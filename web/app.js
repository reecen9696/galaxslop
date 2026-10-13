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

function say(text) {
  status.textContent = text;
}

async function gameDir(create) {
  const root = await navigator.storage.getDirectory();
  const smg = await root.getDirectoryHandle("smg", { create });
  return smg.getDirectoryHandle("game", { create });
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
    say("That folder has no manifest.json. Pick the folder cook.py wrote.");
    return false;
  }
  const manifest = JSON.parse(await manifestEntry.file.text());
  const byPath = new Map(files.map((f) => [f.path, f.file]));
  const missing = manifest.files.filter((e) => !byPath.has(e.path) || byPath.get(e.path).size !== e.size);
  if (missing.length) {
    say(`${missing.length} files are missing or the wrong size (first: ${missing[0].path}). Re-run cook.py.`);
    return false;
  }
  if (navigator.storage.persist) await navigator.storage.persist();
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

function showImport() {
  importPanel.style.display = "block";
  say("");
  const input = document.getElementById("pickInput");
  return new Promise((resolve) => {
    document.getElementById("pick").onclick = async () => {
      if (window.showDirectoryPicker) {
        let dir;
        try {
          dir = await showDirectoryPicker({ id: "smg-game" });
        } catch {
          return; // cancelled
        }
        if (await importFolder(dir)) resolve();
      } else {
        input.click();
      }
    };
    input.onchange = async () => {
      if (input.files.length && (await importFolder([...input.files]))) resolve();
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
    node.connect(ctx.destination);
    await ctx.resume();
    console.log(`web: audio on, ${ctx.sampleRate} Hz`);
  } catch (e) {
    startAudio.started = false;
    console.log("web: audio failed: " + e);
  }
}
for (const ev of ["keydown", "pointerdown"]) addEventListener(ev, startAudio, { capture: true });

function boot(args) {
  importPanel.style.display = "none";
  const canvas = document.getElementById("canvas");
  canvas.style.display = "block";
  canvas.focus();
  window.Module = {
    canvas,
    arguments: args,
    print: (t) => console.log(t),
    printErr: (t) => {
      console.log(t);
      smgLog.push(t);
      if (smgLog.length > 2000) smgLog.shift();
      if (/joined room|present:|disc:|web:/.test(t)) say(t);
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

(async () => {
  if (!crossOriginIsolated) {
    say("This page needs cross-origin isolation (COOP/COEP headers): serve it with web/dev-server.mjs.");
    return;
  }
  if (params.get("source") === "server") {
    boot([]);
    return;
  }
  if (params.get("reimport")) {
    const root = await navigator.storage.getDirectory();
    const smg = await root.getDirectoryHandle("smg", { create: true });
    await smg.removeEntry("game", { recursive: true }).catch(() => {});
  }
  if (!(await importedManifest())) {
    await showImport();
    // Start the game in a fresh page: the import leaves a gigabyte of file
    // data behind in this one.
    location.replace(location.href.replace(/[?&]reimport=1/, ""));
    return;
  }
  boot(["--game", "/opfs/smg/game"]);
})();
