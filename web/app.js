// Page logic for the browser build (P0-07/P0-08): finds the game files,
// imports them into the origin-private file system (OPFS) on first use, then
// loads smg.js.  The wasm side mounts OPFS at /opfs (main_web.cpp).
//
//   ?source=server   read the dev server's game folder instead of OPFS
//                    (web/dev-server.mjs with SMG_GAME; used by tools/web/smoke.mjs)
//   ?reimport=1      forget the imported files and ask again
//   ?PETARI_*=, ?SMG_*=  environment variables for the game
"use strict";

const status = document.getElementById("status");
const importPanel = document.getElementById("import");
const progress = document.getElementById("progress");
const params = new URLSearchParams(location.search);
var smgLog = []; // recent game log lines, for automated checks

// While the game runs the status line floats over the bottom of the canvas
// and fades a few seconds after each message.
function say(text) {
  // In game only problems reach the screen; progress lines (present:,
  // disc:, web:, joined room) stay in the console and the room panel.
  if (document.body.classList.contains("playing") && text && !/abort|fail|error|lost|could not|can't/i.test(text)) return;
  status.textContent = text;
  status.classList.remove("faded");
  clearTimeout(say.timer);
  if (document.body.classList.contains("playing")) say.timer = setTimeout(() => status.classList.add("faded"), 4000);
}

async function gameDir(create) {
  const root = await navigator.storage.getDirectory();
  const smg = await root.getDirectoryHandle("smg", { create });
  return smg.getDirectoryHandle("game", { create });
}

async function importedManifestText() {
  try {
    const dir = await gameDir(false);
    return await (await (await dir.getFileHandle("manifest.json")).getFile()).text();
  } catch {
    return null;
  }
}

// Pre-loaded game (GAME_DIR on the server, SMG_GAME on the dev server): the
// page copies the server's cooked folder into OPFS by itself on the first
// visit, and again only when the server's manifest changes.
async function importFromServer(base, manifestText) {
  const manifest = JSON.parse(manifestText);
  navigator.storage.persist?.().catch(() => {});
  const root = await navigator.storage.getDirectory();
  const smg = await root.getDirectoryHandle("smg", { create: true });
  await smg.removeEntry("game", { recursive: true }).catch(() => {});
  const game = await smg.getDirectoryHandle("game", { create: true });
  const total = manifest.files.reduce((n, e) => n + e.size, 0);
  let done = 0;
  const started = performance.now();
  progress.style.display = "block";
  showLoading();
  const dirs = new Map([["", game]]);
  async function dirFor(path) {
    let key = "", dir = game;
    for (const part of path.split("/").slice(0, -1)) {
      key += part + "/";
      if (!dirs.has(key)) dirs.set(key, await dir.getDirectoryHandle(part, { create: true }));
      dir = dirs.get(key);
    }
    return dir;
  }
  const queue = manifest.files.slice();
  await Promise.all(Array.from({ length: 6 }, async () => {
    while (queue.length) {
      const e = queue.shift();
      const r = await fetch(base + e.path.split("/").map(encodeURIComponent).join("/"));
      if (!r.ok) throw new Error(`${e.path}: HTTP ${r.status}`);
      const bytes = new Uint8Array(await r.arrayBuffer());
      if (bytes.byteLength !== e.size) throw new Error(`${e.path}: ${bytes.byteLength} bytes, manifest says ${e.size}`);
      const w = await (await (await dirFor(e.path)).getFileHandle(e.path.split("/").pop(), { create: true })).createWritable();
      if (bytes.byteLength) await w.write(bytes);
      await w.close();
      done += e.size;
      progress.value = done / total;
      say(`loading the game ${(done / 2 ** 20).toFixed(0)} / ${(total / 2 ** 20).toFixed(0)} MB`);
    }
  }));
  const mw = await (await game.getFileHandle("manifest.json", { create: true })).createWritable();
  await mw.write(manifestText);
  await mw.close();
  progress.style.display = "none";
  console.log(`web: game pre-loaded from the server: ${manifest.files.length} files in ${((performance.now() - started) / 1000).toFixed(1)} s`);
}

async function importedManifest() {
  try {
    const dir = await gameDir(false);
    const file = await (await dir.getFileHandle("manifest.json")).getFile();
    return JSON.parse(await file.text());
  } catch {
    return null;
  }
}

// Files from a directory picker (FileSystemDirectoryHandle) or an
// <input webkitdirectory> (File with webkitRelativePath), as
// [{ path: "files/...", file: File }] relative to the folder holding manifest.json.
async function listPicked(source) {
  const out = [];
  if (Array.isArray(source)) {
    for (const f of source) out.push({ path: f.webkitRelativePath.split("/").slice(1).join("/"), file: f });
  } else {
    async function walk(dir, prefix) {
      for await (const [name, h] of dir.entries()) {
        if (h.kind === "directory") await walk(h, prefix + name + "/");
        else out.push({ path: prefix + name, file: await h.getFile() });
      }
    }
    await walk(source, "");
  }
  return out;
}

async function importFolder(source) {
  say("reading folder…");
  const files = await listPicked(source);
  const manifestEntry = files.find((f) => f.path === "manifest.json");
  if (!manifestEntry) {
    if (files.some((f) => /(^|\/)sys\/fst\.bin$/.test(f.path))) return cookFolder(files);
    say("That folder is neither an extracted disc (sys/, files/) nor a converted one (manifest.json).");
    return false;
  }
  const manifest = JSON.parse(await manifestEntry.file.text());
  const byPath = new Map(files.map((f) => [f.path, f.file]));
  const missing = manifest.files.filter((e) => !byPath.has(e.path) || byPath.get(e.path).size !== e.size);
  if (missing.length) {
    say(`${missing.length} files are missing or the wrong size (first: ${missing[0].path}). Re-run cook.py.`);
    return false;
  }
  // Ask for persistent storage, but never wait on it: Firefox shows a prompt.
  navigator.storage.persist?.().catch(() => {});
  const est = navigator.storage.estimate ? await navigator.storage.estimate() : null;
  const total = manifest.files.reduce((n, e) => n + e.size, 0);
  if (est && est.quota - est.usage < total) {
    say(`Not enough browser storage: need ${(total / 2 ** 30).toFixed(1)} GB.`);
    return false;
  }
  // Start clean, copy every file, write manifest.json last so a cut-short
  // import is never mistaken for a finished one.
  const root = await navigator.storage.getDirectory();
  const smg = await root.getDirectoryHandle("smg", { create: true });
  await smg.removeEntry("game", { recursive: true }).catch(() => {});
  const game = await smg.getDirectoryHandle("game", { create: true });
  progress.style.display = "block";
  let done = 0;
  const started = performance.now();
  const dirs = new Map([["", game]]);
  async function dirFor(path) {
    const parts = path.split("/").slice(0, -1);
    let key = "", dir = game;
    for (const p of parts) {
      key += p + "/";
      if (!dirs.has(key)) dirs.set(key, await dir.getDirectoryHandle(p, { create: true }));
      dir = dirs.get(key);
    }
    return dir;
  }
  async function copy(entry) {
    const dir = await dirFor(entry.path);
    const fh = await dir.getFileHandle(entry.path.split("/").pop(), { create: true });
    const w = await fh.createWritable();
    await byPath.get(entry.path).stream().pipeTo(w);
    done += entry.size;
    progress.value = done / total;
    say(`copying ${(done / 2 ** 20).toFixed(0)} / ${(total / 2 ** 20).toFixed(0)} MB`);
  }
  const queue = manifest.files.slice();
  await Promise.all(Array.from({ length: 6 }, async () => {
    while (queue.length) await copy(queue.shift());
  }));
  const mh = await game.getFileHandle("manifest.json", { create: true });
  const mw = await mh.createWritable();
  await mw.write(await manifestEntry.file.text());
  await mw.close();
  progress.style.display = "none";
  console.log(`web: imported ${manifest.files.length} files in ${((performance.now() - started) / 1000).toFixed(1)} s`);
  return true;
}

// A Dolphin-extracted disc: convert it in the browser (web/cook/cooker.js,
// tools/cook/cook.py under Pyodide) straight into OPFS.
async function cookFolder(files) {
  // Ask for persistent storage, but never wait on it: Firefox shows a prompt.
  navigator.storage.persist?.().catch(() => {});
  const root = await navigator.storage.getDirectory();
  const smg = await root.getDirectoryHandle("smg", { create: true });
  await smg.removeEntry("game", { recursive: true }).catch(() => {});
  const game = await smg.getDirectoryHandle("game", { create: true });
  progress.style.display = "block";
  say("starting the converter…");
  try {
    const r = await cookDisc(files, game, (fraction, text) => {
      progress.value = fraction;
      say(text);
    });
    console.log(`web: converted the disc: ${r.files} files in ${r.seconds.toFixed(0)} s`);
    return true;
  } catch (e) {
    console.log("web: conversion failed: " + e.message);
    say("Conversion failed: " + e.message);
    await smg.removeEntry("game", { recursive: true }).catch(() => {});
    return false;
  } finally {
    progress.style.display = "none";
  }
}

function showImport() {
  importPanel.style.display = "block";
  startMenuMusic();
  say("");
  const input = document.getElementById("pickInput");
  // The chosen folder is copied or converted under the loading screen; on a
  // problem the import screen comes back with the message.
  async function load(source) {
    importPanel.style.display = "none";
    showLoading();
    if (await importFolder(source)) return true;
    hideLoading();
    importPanel.style.display = "block";
    startMenuMusic("menu");
    return false;
  }
  return new Promise((resolve) => {
    document.getElementById("pick").onclick = async () => {
      if (window.showDirectoryPicker) {
        let dir;
        try {
          dir = await showDirectoryPicker({ id: "smg-game" });
        } catch {
          return; // cancelled
        }
        if (await load(dir)) resolve();
      } else {
        input.click();
      }
    };
    input.onchange = async () => {
      if (input.files.length && (await load([...input.files]))) resolve();
    };
  });
}

// Audio (P1-01): an AudioWorklet reads the game's PCM ring straight out of
// wasm memory (platform/src/web/audio_web.cpp) and resamples it linearly to
// the context rate.  Browsers only start audio after a user gesture.
const kWorklet = `
class SmgAudio extends AudioWorkletProcessor {
  constructor(opts) {
    super();
    const { buffer, ring, frames } = opts.processorOptions;
    this.u32 = new Uint32Array(buffer, ring, 5);
    this.pcm = new Int16Array(buffer, ring + 20, frames * 2);
    this.mask = frames - 1;
    this.pos = 0;  // fractional read position within the current frame
    Atomics.store(this.u32, 4, 1);  // attached
  }
  process(_inputs, outputs) {
    const out = outputs[0], L = out[0], R = out[1] || out[0];
    const u = this.u32, pcm = this.pcm, mask = this.mask;
    const step = (Atomics.load(u, 2) || 32000) / sampleRate;
    let read = Atomics.load(u, 1);
    const write = Atomics.load(u, 0);
    for (let i = 0; i < L.length; i++) {
      if (write - read < 2) {
        L[i] = R[i] = 0;
        if (i === 0 && write !== 0) Atomics.add(u, 3, 1);
        continue;
      }
      const a = (read & mask) * 2, b = ((read + 1) & mask) * 2, t = this.pos;
      L[i] = (pcm[a] * (1 - t) + pcm[b] * t) / 32768;
      R[i] = (pcm[a + 1] * (1 - t) + pcm[b + 1] * t) / 32768;
      this.pos += step;
      const whole = Math.floor(this.pos);
      read += whole;
      this.pos -= whole;
    }
    Atomics.store(u, 1, read);
    return true;
  }
}
registerProcessor("smg-audio", SmgAudio);
`;

let smgGain = null;

async function startAudio() {
  if (startAudio.started || !window.Module || !Module._web_audio_ring || !Module.HEAPU8) return;
  startAudio.started = true;
  try {
    const ctx = new AudioContext({ latencyHint: "interactive" });
    const url = URL.createObjectURL(new Blob([kWorklet], { type: "text/javascript" }));
    await ctx.audioWorklet.addModule(url);
    const node = new AudioWorkletNode(ctx, "smg-audio", {
      outputChannelCount: [2],
      // The ring sits above 2 GB (the game image is at 0x98000000): read the
      // returned pointer as unsigned.
      processorOptions: { buffer: Module.HEAPU8.buffer, ring: Module._web_audio_ring() >>> 0, frames: Module._web_audio_ring_frames() },
    });
    node.onprocessorerror = (e) => console.log("web: audio worklet failed: " + (e.message || e));
    smgGain = ctx.createGain();
    smgGain.gain.value = Number(store("smg.volume") ?? 0.8);
    node.connect(smgGain).connect(ctx.destination);
    await ctx.resume();
    console.log(`web: audio on, ${ctx.sampleRate} Hz`);
  } catch (e) {
    startAudio.started = false;
    console.log("web: audio failed: " + e);
  }
}
for (const ev of ["keydown", "pointerdown"]) addEventListener(ev, startAudio, { capture: true });

// Music: the Wii Menu theme loops on the opening screens (import, lobby),
// Comet Observatory, a little quieter, on the first-visit loading screen;
// both fade out when the game starts.  The files (web/music/) are not in git
// (copyrighted); without them the page is silent.  Browsers block autoplay
// until the first click or key, so a blocked start shows a hint and retries
// then.
const bgm = document.getElementById("bgm");
const musicHint = document.getElementById("musicHint");
const kTracks = { menu: { src: "music/wii-menu.mp3", level: 0.6 }, loading: { src: "music/comet-observatory.mp3", level: 0.35 } };
const missingTracks = new Set();
let track = "";
const musicOn = () => store("smg.music") !== "0";
const musicVolume = () => (kTracks[track]?.level ?? 0.6) * Number(store("smg.volume") ?? 0.8);
function startMenuMusic(name = "menu") {
  if (bgm.dataset.state === "done") return;
  if (name !== track) {
    track = name;
    bgm.src = kTracks[name].src;
    if (bgm.dataset.state !== "waiting") bgm.dataset.state = "";
  }
  if (!musicOn() || missingTracks.has(track)) return;
  bgm.volume = musicVolume();
  bgm.play().then(() => {
    bgm.dataset.state = "playing";
    musicHint.hidden = true;
  }).catch(() => {
    if (bgm.dataset.state === "waiting" || missingTracks.has(track)) return;
    bgm.dataset.state = "waiting";
    musicHint.hidden = false;
    const retry = () => {
      removeEventListener("pointerdown", retry, true);
      removeEventListener("keydown", retry, true);
      musicHint.hidden = true;
      bgm.dataset.state = "";
      startMenuMusic(track);
    };
    addEventListener("pointerdown", retry, true);
    addEventListener("keydown", retry, true);
  });
}
function stopMenuMusic(fadeMs = 800) {
  bgm.dataset.state = "done";
  musicHint.hidden = true;
  if (bgm.paused) return;
  const v0 = bgm.volume, t0 = performance.now();
  const step = () => {
    const t = Math.min(1, (performance.now() - t0) / fadeMs);
    bgm.volume = v0 * (1 - t);
    if (t < 1) requestAnimationFrame(step);
    else bgm.pause();
  };
  requestAnimationFrame(step);
}
bgm.addEventListener("error", () => {
  missingTracks.add(track);
  musicHint.hidden = true;
});

// First-visit loading screen: Luma dances above the bar (web/luma.js, which
// pulls in three.js only now) to Comet Observatory.  ?demo=loading shows it
// with a fake bar, to look at without clearing browser storage.
let luma = null;
function showLoading() {
  document.body.classList.add("loading");
  startMenuMusic("loading");
  // A spinner holds Luma's place while three.js and the model download.
  const holder = document.getElementById("luma");
  if (!luma) holder.classList.add("waiting");
  luma ??= import("./luma.js")
    .then((m) => m.startLuma(holder))
    .catch((e) => {
      console.log("web: no dancing Luma: " + e.message);
      return null;
    })
    .finally(() => holder.classList.remove("waiting"));
}
function hideLoading() {
  document.body.classList.remove("loading");
  luma?.then((l) => l?.stop());
  luma = null;
}
function runLoadingDemo() {
  showLoading();
  progress.style.display = "block";
  const total = 1180, t0 = performance.now();
  setInterval(() => {
    const f = ((performance.now() - t0) / 30000) % 1;
    progress.value = f;
    say(`loading the game ${Math.round(f * total)} / ${total} MB`);
  }, 100);
}

// Lobby (P2-06): name, room code and relay, remembered in localStorage.
// Skipped when the URL already says (?SMG_RELAY=... or ?solo=1).
function store(key, value) {
  try {
    if (value === undefined) return localStorage.getItem(key);
    localStorage.setItem(key, value);
  } catch {}
  return null;
}

// The relay: on this origin when the page is served by it (server/,
// STATIC_DIR; it answers config.json), else a local relay on 8787 (dev).
let defaultRelay = `${location.protocol === "https:" ? "wss" : "ws"}://${location.hostname}:8787`;
fetch("config.json")
  .then((r) => (r.ok ? r.json() : null))
  .then((c) => {
    if (c && c.relay === "same-origin") defaultRelay = `${location.protocol === "https:" ? "wss" : "ws"}://${location.host}/`;
    const relay = document.getElementById("relay");
    if (relay && !store("smg.relay")) relay.value = defaultRelay;
  })
  .catch(() => {});

function showLobby() {
  const lobby = document.getElementById("lobby");
  const name = document.getElementById("name");
  const room = document.getElementById("room");
  const relay = document.getElementById("relay");
  name.value = store("smg.name") || "";
  document.getElementById("contact").checked = store("smg.contact") !== "0";
  room.value = params.get("room") || "";
  const tintBox = document.getElementById("tint");
  const savedTint = Number(store("smg.tint")) || 0;
  tintBox.innerHTML = kTints
    .map((t, i) => `<label class="swatch" title="${t.name}"><input type="radio" name="tint" value="${i}" aria-label="${t.name}"${i === savedTint ? " checked" : ""}>`
      + `<span${t.hex ? ` style="background:${t.hex}"` : ""}></span></label>`)
    .join("");
  relay.value = store("smg.relay") || defaultRelay;
  lobby.style.display = "block";
  startMenuMusic();
  say("");
  const serverBtn = document.getElementById("serverBtn"), serverBox = document.getElementById("serverBox");
  serverBtn.onclick = () => {
    const open = serverBox.classList.toggle("open");
    serverBtn.setAttribute("aria-expanded", String(open));
    if (open) relay.focus();
  };
  if (!name.value) name.focus();
  else if (room.value) document.getElementById("join").focus();
  name.addEventListener("keydown", (e) => {
    if (e.key === "Enter") room.focus();
  });
  return new Promise((resolve) => {
    const go = (mode) => {
      const n = name.value.trim() || "Player";
      store("smg.name", n);
      store("smg.relay", relay.value.trim());
      const tint = tintBox.querySelector("input:checked")?.value || "0";
      store("smg.tint", tint);
      lobby.style.display = "none";
      if (mode === "solo") return resolve({});
      const contact = document.getElementById("contact").checked;
      store("smg.contact", contact ? "1" : "0");
      resolve({ SMG_RELAY: relay.value.trim(), SMG_ROOM: mode === "join" ? room.value.trim().toUpperCase() : "", SMG_NAME: n, SMG_TINT: tint, SMG_CONTACT: contact ? "1" : "0" });
    };
    document.getElementById("join").onclick = () => (room.value.trim() ? go("join") : room.focus());
    document.getElementById("create").onclick = () => go("create");
    document.getElementById("solo").onclick = () => go("solo");
    // Not `onkeydown = (e) => cond && ...`: an on-handler returning false
    // cancels the key, which made the field refuse every letter.
    room.addEventListener("keydown", (e) => {
      if (e.key === "Enter") document.getElementById("join").click();
    });
  });
}

// Room panel, floating top right next to the settings gear, fed by the
// game's net: log lines.
const roombar = document.getElementById("roombar");
function toast(text) {
  const el = document.createElement("div");
  el.className = "toast";
  el.textContent = text;
  document.body.appendChild(el);
  setTimeout(() => el.remove(), 5000);
}

function onNetLine(t) {
  let m;
  if ((m = /^net: power star (\S+) (\d+) from (.+)$/.exec(t))) return toast(`${m[3]} got a Power Star (${m[1].replace(/Galaxy$/, " Galaxy")}, star ${m[2]})`);
  if ((m = /^net: grand star (\d+) from (.+)$/.exec(t))) return toast(`${m[2]} got Grand Star ${m[1]}`);
  if ((m = /^net: joined room (\S+) as player/.exec(t))) {
    roombar.dataset.room = m[1];
  } else if (/^net: .+ (joined \(player|left$)/.test(t)) {
    // the count comes from the game's remote list; just redraw
  } else if (/^net: connected to /.test(t)) {
    roombar.dataset.state = "joining…";
  } else if (/^net: connection lost/.test(t) || (m = /^net: relay closed the connection/.exec(t))) {
    roombar.dataset.room = "";
    roombar.dataset.state = "reconnecting…";
  } else if ((m = /^net: relay error \d+: (.+)$/.exec(t))) {
    roombar.dataset.state = "!" + m[1];
  } else {
    return;
  }
  renderRoomBar();
}

// Room bar: the code and each friend with where they are; Follow takes you
// to their galaxy and star (P3-04, present_web.cpp's player list).
const pretty = (stage) => stage.replace(/([a-z])([A-Z0-9])/g, "$1 $2");
function readRemotes() {
  if (!window.Module || !Module._web_remote_list || !Module.HEAPU8) return [];
  const ptr = Module._web_remote_list() >>> 0;
  const v = new DataView(Module.HEAPU8.buffer, ptr, 4 + 96 * 4);
  const str = (o, n) => {
    const b = new Uint8Array(Module.HEAPU8.buffer, ptr + o, n).slice();
    const z = b.indexOf(0);
    return new TextDecoder().decode(z < 0 ? b : b.subarray(0, z));
  };
  const out = [];
  for (let i = 0; i < v.getUint32(0, true); i++) {
    const o = 4 + 96 * i;
    out.push({ slot: v.getInt32(o, true), scenario: v.getInt32(o + 4, true), same: v.getUint32(o + 8, true) === 1, name: str(o + 12, 36), stage: str(o + 48, 48) });
  }
  return out;
}
const kPersonIcon = `<svg viewBox="0 0 24 24" aria-label="players" role="img"><circle cx="12" cy="7.5" r="4.5"/><path d="M3 21c0-5 4-8 9-8s9 3 9 8z"/></svg>`;
function renderRoomBar() {
  const code = roombar.dataset.room;
  if (!code) {
    // In co-op the panel shows from the start: connecting, joining, errors.
    if (!roombar.dataset.mode) return;
    roombar.style.display = "block";
    const st = roombar.dataset.state || "connecting…";
    const html = `<div class="rb-head">${roombar.dataset.want ? `<b>${roombar.dataset.want}</b>` : ""}</div>`
      + `<span class="rb-who${st[0] === "!" ? " rb-bad" : ""}">${st.replace(/^!/, "").replace(/[<&"]/g, "")}</span>`;
    if (roombar.innerHTML !== html) roombar.innerHTML = html;
    return;
  }
  roombar.dataset.state = "";
  roombar.style.display = "block";
  const remotes = readRemotes();
  const esc = (t) => t.replace(/[<&"]/g, "");
  // The head has the code and how many are in the room (you included),
  // names on hover; below, only friends in another level, to Follow.
  const who = remotes
    .filter((r) => !r.same && r.stage)
    .map((r) => `<span>${esc(r.name)} in ${esc(pretty(r.stage))} ${r.scenario} <button class="wii-btn follow" data-slot="${r.slot}">Follow</button></span>`)
    .join("<br>");
  const names = esc(remotes.map((r) => r.name).join(", ")) || "just you";
  const html = `<div class="rb-head"><b>${code}</b><span class="rb-count" title="${names}">${kPersonIcon}${remotes.length + 1}</span></div><span class="rb-who">${who}</span>`;
  if (roombar.innerHTML !== html) roombar.innerHTML = html;
}
roombar.addEventListener("click", (e) => {
  const b = e.target.closest(".follow");
  if (b && window.Module && Module._web_follow) {
    Module._web_follow(Number(b.dataset.slot));
    b.textContent = "Going…";
  }
});
setInterval(renderRoomBar, 1000);

// Name tags over remote players (P2-07), from present_web.cpp's projection.
// Player colours, indexed by tint (net/puppet/puppet_tint.h): the lobby's
// swatches and the name tags.  Auto is by join order; the tag gets the
// resolved tint.  Black's tag is grey to read on the dark label.
const kTints = [
  { name: "Auto (by join order)" },
  { name: "Classic", hex: "#e3342f" },
  { name: "Green (red on Luigi)", hex: "#3cb043" },
  { name: "Pink", hex: "#ff85c0" },
  { name: "Orange", hex: "#ff9a2e" },
  { name: "Purple", hex: "#9b59d0" },
  { name: "Cyan", hex: "#2ec4e6" },
  { name: "Yellow", hex: "#ffd23f" },
  { name: "White", hex: "#f4f4f4" },
  { name: "Black", hex: "#2b2b2b", tag: "#a0a4aa" },
];
function drawTags() {
  requestAnimationFrame(drawTags);
  const box = document.getElementById("tags");
  if (!window.Module || !Module._web_name_tags || !Module.HEAPU8) return;
  const ptr = Module._web_name_tags() >>> 0;
  const v = new DataView(Module.HEAPU8.buffer, ptr, 4 + 48 * 4);
  const n = v.getUint32(0, true);
  while (box.children.length < n) box.appendChild(document.createElement("span"));
  for (let i = 0; i < box.children.length; i++) {
    const el = box.children[i];
    if (i >= n) {
      el.style.display = "none";
      continue;
    }
    const o = 4 + 48 * i;
    const bytes = new Uint8Array(Module.HEAPU8.buffer, ptr + o + 12, 36);
    const name = new TextDecoder().decode(bytes.slice(0, bytes.indexOf(0) < 0 ? 36 : bytes.indexOf(0)));
    el.textContent = name;
    const t = kTints[v.getUint32(o + 8, true)] || kTints[1];
    el.style.color = t.tag || t.hex;
    el.style.left = ((v.getFloat32(o, true) + 1) * 50).toFixed(2) + "%";
    el.style.top = ((1 - v.getFloat32(o + 4, true)) * 50).toFixed(2) + "%";
    el.style.display = "block";
  }
}

function boot(args, env = {}) {
  window.smgEnv = { SMG_SCALE: store("smg.scale") || "auto", ...env }; // read by browser_env.js in preRun
  importPanel.style.display = "none";
  stopMenuMusic();
  document.body.classList.add("playing");
  status.textContent = "";
  if (env.SMG_RELAY) {
    roombar.dataset.mode = "net";
    roombar.dataset.want = (env.SMG_ROOM || "").replace(/[^A-Z0-9]/g, "");
    renderRoomBar();
  }
  const canvas = document.getElementById("canvas");
  canvas.style.display = "block";
  document.getElementById("stage").style.display = "block";
  requestAnimationFrame(drawTags);

  // Pointer lock for input capture - native browser API
  let pointerLocked = false;

  if (canvas.requestPointerLock) {
    // Block destructive shortcuts while in pointer lock state
    document.addEventListener("keydown", (e) => {
      // Only block when pointer is locked (browser-level focus)
      if (!pointerLocked) return;

      // Dangerous actions to prevent:
      if ((e.ctrlKey && e.key.toLowerCase() === "w") ||        // close tab
          (e.ctrlKey && e.shiftKey && e.key.toLowerCase() === "t") ||  // reopen tabs
          (e.ctrlKey && e.key.toLowerCase() === "d") ||   // bookmark page
          (e.key === "F11") ||                    // browser fullscreen
          (e.altKey && e.key.toLowerCase() === "f4")) {     // quit window
        e.preventDefault();
        return false;
      }
    }, { capture: true });

    // Release pointer lock on Escape - allows clean exit and recapture
    document.addEventListener("keydown", (e) => {
      if (e.key === "Escape" && pointerLocked) {
        // Exit pointer lock - this is the proper way to release it
        document.exitPointerLock().then(() => {
          pointerLocked = false;
          canvas.focus();
          // The game can now handle its quit logic normally
        }).catch((err) => {
          console.log("web: Pointer Lock API error:", err.message);
        });
        return true;  // Consume the Escape event while in pointer lock
      }
    }, { capture: true });

    // Request pointer lock when clicking anywhere on canvas during gameplay
    canvas.addEventListener("click", () => {
      if (pointerLocked) return;
      canvas.requestPointerLock().then(() => {
        pointerLocked = true;
      }).catch((e) => {
        console.log("web: Pointer Lock request blocked:", e.message);
      });
    }, { capture: true });

    // Also support keyboard-only recapture (Tab to canvas then click, or just use game controls)
    canvas.addEventListener("blur", () => {
      pointerLocked = false;
      document.exitPointerLock().then(() => {});
    }, { capture: true });
  } else {
    console.log("web: Pointer Lock API not available in this browser");
  }

  canvas.focus();

  window.Module = {
    canvas,
    arguments: args,
    print: (t) => console.log(t),
    printErr: (t) => {
      console.log(t);
      smgLog.push(t);
      if (t.startsWith("net: ")) onNetLine(t);
      if (smgLog.length > 2000) smgLog.shift();
      if (/joined room|present:|disc:|web:/.test(t) && !/display frames|fps,/.test(t)) say(t);
    },
    onAbort: (e) => say("aborted: " + e),
    onRuntimeInitialized: () => {
      if (params.get("audio") === "auto") startAudio();  // automated runs (autoplay allowed by flag)
    },
  };
  const s = document.createElement("script");
  s.src = "smg.js";
  document.body.appendChild(s);
}

// Settings (P1-09, page side): volume, save export/import, re-import, delete.
// Saves live in OPFS /smg/save (main_web.cpp); export packs them into an
// uncompressed zip, import unpacks one and restarts the page so the game
// reads it at boot.
const crcTable = Array.from({ length: 256 }, (_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});
function crc32(bytes) {
  let c = 0xffffffff;
  for (const b of bytes) c = crcTable[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
function zipStore(files) {
  const enc = new TextEncoder(), parts = [], central = [];
  let offset = 0;
  for (const { name, data } of files) {
    const n = enc.encode(name), crc = crc32(data);
    const h = new DataView(new ArrayBuffer(30));
    h.setUint32(0, 0x04034b50, true); h.setUint16(4, 20, true); h.setUint32(14, crc, true);
    h.setUint32(18, data.length, true); h.setUint32(22, data.length, true); h.setUint16(26, n.length, true);
    parts.push(new Uint8Array(h.buffer), n, data);
    const c = new DataView(new ArrayBuffer(46));
    c.setUint32(0, 0x02014b50, true); c.setUint16(4, 20, true); c.setUint16(6, 20, true); c.setUint32(16, crc, true);
    c.setUint32(20, data.length, true); c.setUint32(24, data.length, true); c.setUint16(28, n.length, true); c.setUint32(42, offset, true);
    central.push(new Uint8Array(c.buffer), n);
    offset += 30 + n.length + data.length;
  }
  const size = central.reduce((s, p) => s + p.length, 0);
  const e = new DataView(new ArrayBuffer(22));
  e.setUint32(0, 0x06054b50, true); e.setUint16(8, files.length, true); e.setUint16(10, files.length, true);
  e.setUint32(12, size, true); e.setUint32(16, offset, true);
  return new Blob([...parts, ...central, new Uint8Array(e.buffer)], { type: "application/zip" });
}
function zipRead(buf) {
  const v = new DataView(buf), out = [], dec = new TextDecoder();
  let p = 0;
  while (p + 30 <= buf.byteLength && v.getUint32(p, true) === 0x04034b50) {
    if (v.getUint16(p + 8, true) !== 0) throw new Error("compressed zip entries are not supported: export from this page");
    const size = v.getUint32(p + 18, true), nlen = v.getUint16(p + 26, true), xlen = v.getUint16(p + 28, true);
    const name = dec.decode(new Uint8Array(buf, p + 30, nlen));
    out.push({ name, data: new Uint8Array(buf.slice(p + 30 + nlen + xlen, p + 30 + nlen + xlen + size)) });
    p += 30 + nlen + xlen + size;
  }
  return out;
}
async function saveDir(create) {
  const root = await navigator.storage.getDirectory();
  return (await root.getDirectoryHandle("smg", { create })).getDirectoryHandle("save", { create });
}
async function exportSave() {
  const files = [];
  async function walk(dir, prefix) {
    for await (const [name, h] of dir.entries()) {
      if (h.kind === "directory") await walk(h, prefix + name + "/");
      else files.push({ name: prefix + name, data: new Uint8Array(await (await h.getFile()).arrayBuffer()) });
    }
  }
  try {
    await walk(await saveDir(false), "");
  } catch {}
  if (!files.length) return say("No save data yet.");
  const a = document.createElement("a");
  a.href = URL.createObjectURL(zipStore(files));
  a.download = `smg-save-${new Date().toISOString().slice(0, 10)}.zip`;
  a.click();
}
async function importSave(file) {
  const entries = zipRead(await file.arrayBuffer());
  if (!entries.some((e) => /GameData\.bin$/.test(e.name))) return say("That zip holds no Super Mario Galaxy save (GameData.bin).");
  const root = await navigator.storage.getDirectory();
  const smg = await root.getDirectoryHandle("smg", { create: true });
  await smg.removeEntry("save", { recursive: true }).catch(() => {});
  const save = await smg.getDirectoryHandle("save", { create: true });
  for (const { name, data } of entries) {
    if (name.endsWith("/") || name.includes("..")) continue;
    let dir = save;
    const parts = name.split("/");
    for (const part of parts.slice(0, -1)) dir = await dir.getDirectoryHandle(part, { create: true });
    const w = await (await dir.getFileHandle(parts.at(-1), { create: true })).createWritable();
    await w.write(data);
    await w.close();
  }
  location.reload();
}
function setupSettings() {
  const gear = document.getElementById("gear"), panel = document.getElementById("settings");
  gear.onclick = () => {
    const open = panel.style.display !== "block";
    panel.style.display = open ? "block" : "none";
    gear.setAttribute("aria-expanded", String(open));
  };
  const vol = document.getElementById("volume");
  vol.value = store("smg.volume") ?? 0.8;
  vol.oninput = () => {
    store("smg.volume", vol.value);
    if (smgGain) smgGain.gain.value = Number(vol.value);
    if (bgm.dataset.state !== "done") bgm.volume = musicVolume();
  };
  const music = document.getElementById("music");
  music.checked = musicOn();
  music.onchange = () => {
    store("smg.music", music.checked ? "1" : "0");
    if (!music.checked) bgm.pause();
    else if (bgm.dataset.state !== "done") startMenuMusic();
  };
  // Render scale (applied live by present_web.cpp) and the frame-time overlay.
  const scale = document.getElementById("scale");
  scale.value = store("smg.scale") || "auto";
  scale.onchange = () => {
    store("smg.scale", scale.value);
    if (!window.Module || !Module._web_request_scale) return;
    if (scale.value === "auto") Module._web_set_auto_scale(2);
    else Module._web_request_scale(Number(scale.value));
  };
  // Picture: 16:9 (the game's own shape) or stretched over the window, and
  // browser full screen (Esc leaves it).
  const picture = document.getElementById("picture");
  picture.value = store("smg.picture") === "stretch" ? "stretch" : "wide";
  const applyPicture = () => document.body.classList.toggle("stretch", picture.value === "stretch");
  picture.onchange = () => {
    store("smg.picture", picture.value);
    applyPicture();
  };
  applyPicture();
  const fs = document.getElementById("fullscreen");
  fs.onclick = () => {
    if (document.fullscreenElement) document.exitFullscreen();
    else document.documentElement.requestFullscreen?.().catch(() => {});
  };
  document.addEventListener("fullscreenchange", () => {
    fs.textContent = document.fullscreenElement ? "Leave full screen" : "Full screen";
    document.getElementById("canvas").focus();
  });
  const perf = document.getElementById("perf"), perfbox = document.getElementById("perfbox");
  perf.checked = store("smg.perf") === "1";
  const showPerf = () => {
    store("smg.perf", perf.checked ? "1" : "0");
    perfbox.style.display = perf.checked ? "block" : "none";
  };
  perf.onchange = showPerf;
  showPerf();
  setInterval(() => {
    if (!perf.checked || !window.Module || !Module._web_frame_stats || !Module.HEAPU8) return;
    const v = new DataView(Module.HEAPU8.buffer, Module._web_frame_stats() >>> 0, 24);
    const f = (o) => v.getFloat32(o, true).toFixed(1);
    perfbox.textContent = `${v.getUint32(16, true)} fps   game frame ${v.getUint32(20, true)}\npresent  ${f(0)} / ${f(4)} ms (p50/p95)\ninterval ${f(8)} / ${f(12)} ms`;
  }, 500);
  document.getElementById("exportSave").onclick = exportSave;
  const imp = document.getElementById("importSaveInput");
  document.getElementById("importSave").onclick = () => imp.click();
  imp.onchange = () => imp.files[0] && importSave(imp.files[0]);
  document.getElementById("reimport").onclick = () => {
    location.href = location.pathname + "?reimport=1";
  };
  document.getElementById("deleteAll").onclick = async () => {
    const btn = document.getElementById("deleteAll");
    if (btn.dataset.armed !== "1") {
      btn.dataset.armed = "1";
      btn.textContent = "Click again to delete game files and saves";
      return;
    }
    const root = await navigator.storage.getDirectory();
    await root.removeEntry("smg", { recursive: true }).catch(() => {});
    location.href = location.pathname;
  };
}
setupSettings();

(async () => {
  if (params.get("demo") === "loading") return runLoadingDemo();
  if (!crossOriginIsolated) {
    say("This page needs cross-origin isolation (COOP/COEP headers): serve it with web/dev-server.mjs.");
    return;
  }
  const wantsLobby = !params.get("solo") && !params.get("SMG_RELAY");
  if (params.get("source") === "server") {
    boot([], wantsLobby && params.get("lobby") ? await showLobby() : {});
    return;
  }
  if (params.get("reimport")) {
    const root = await navigator.storage.getDirectory();
    const smg = await root.getDirectoryHandle("smg", { create: true });
    await smg.removeEntry("game", { recursive: true }).catch(() => {});
  }
  const config = await fetch("config.json").then((r) => (r.ok ? r.json() : null)).catch(() => null);
  if (config && config.game) {
    let serverManifest = null;
    try {
      const r = await fetch(config.game + "manifest.json", { cache: "no-cache" });
      if (r.ok) serverManifest = await r.text();
    } catch {}
    if (serverManifest && serverManifest !== (await importedManifestText())) {
      try {
        await importFromServer(config.game, serverManifest);
        location.replace(location.href.replace(/[?&]reimport=1/, ""));
        return;
      } catch (e) {
        console.log("web: pre-loading the game failed: " + e.message);
        hideLoading();
        progress.style.display = "none";
        say("Loading the game from the server failed (" + e.message + "). You can load it from a folder instead.");
      }
    }
  }
  if (!(await importedManifest())) {
    await showImport();
    // Start the game in a fresh page: the import leaves a gigabyte of file
    // data behind in this one.
    location.replace(location.href.replace(/[?&]reimport=1/, ""));
    return;
  }
  boot(["--game", "/opfs/smg/game"], wantsLobby ? await showLobby() : {});
})();
