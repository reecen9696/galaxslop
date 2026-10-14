// Leaderboard (P5-07): the coins and star bits each player picked up
// themselves, best run in this galaxy and in total.  The game counts the
// local player's pickups per run (port/board.h, read through _web_board);
// this file keeps the scores in localStorage, so they last across sessions,
// rooms and relays, and while in a room posts them to the relay
// (server/src/board.ts), whose board lists the players online in it now.
// Opened by the trophy next to the gear, Tab, or a gamepad's Y; shown by
// itself for a few seconds after a star.  Loaded after app.js and uses its
// store(), pretty(), params and roombar.
"use strict";

(() => {
  const btn = document.getElementById("boardBtn");
  const panel = document.getElementById("board");
  const body = document.getElementById("boardBody");
  const timer = document.getElementById("boardTimer");
  const tabs = [...panel.querySelectorAll(".bd-tab")];
  const kAutoMs = 6000;

  // ---- scores, this browser's: {total, galaxies: {Stage: best run}} ----
  const kKey = "smg.board";
  let mine = { total: { coins: 0, bits: 0 }, galaxies: {} };
  try {
    const s = JSON.parse(store(kKey) || "null");
    if (s && s.total && s.galaxies) mine = s;
  } catch {}
  const points = (s) => (s ? s.coins + s.bits : 0);
  let dirty = false;
  let lastStage = mine.lastStage || "";

  function finishRun(stage, coins, bits) {
    if (!stage || coins + bits === 0) return;
    const best = mine.galaxies[stage];
    if (coins + bits > points(best)) {
      mine.galaxies[stage] = { coins, bits };
      dirty = true;
    }
  }

  // The game's PortBoard: u32 run, u32 stars, then the current and the
  // previous run, each {u32 coins, u32 bits, i32 scenario, char stage[48]}.
  function readGame() {
    if (!window.Module || !Module._web_board || !Module.HEAPU8) return null;
    const ptr = Module._web_board() >>> 0;
    const v = new DataView(Module.HEAPU8.buffer, ptr, 128);
    const run = (o) => {
      const b = new Uint8Array(Module.HEAPU8.buffer, ptr + o + 12, 48).slice();
      const z = b.indexOf(0);
      return { coins: v.getUint32(o, true), bits: v.getUint32(o + 4, true), stage: new TextDecoder().decode(z < 0 ? b : b.subarray(0, z)) };
    };
    return { run: v.getUint32(0, true), stars: v.getUint32(4, true), cur: run(8), prev: run(68) };
  }

  let seen = null;  // {run, stars, coins, bits}: what was last added up
  function poll() {
    const g = readGame();
    if (!g) return;
    if (!seen) seen = { run: g.run, stars: g.stars, coins: 0, bits: 0 };
    if (g.run !== seen.run) {
      // The run we were adding up ended: its last pickups and its best.
      if (g.run === seen.run + 1) {
        mine.total.coins += Math.max(0, g.prev.coins - seen.coins);
        mine.total.bits += Math.max(0, g.prev.bits - seen.bits);
        finishRun(g.prev.stage, g.prev.coins, g.prev.bits);
      }
      seen = { run: g.run, stars: seen.stars, coins: 0, bits: 0 };
      dirty = true;
    }
    const dc = g.cur.coins - seen.coins, db = g.cur.bits - seen.bits;
    if (dc > 0 || db > 0) {
      mine.total.coins += Math.max(0, dc);
      mine.total.bits += Math.max(0, db);
      seen.coins = g.cur.coins;
      seen.bits = g.cur.bits;
      finishRun(g.cur.stage, g.cur.coins, g.cur.bits);  // a best is a best as soon as it is reached
      dirty = true;
    }
    if (g.cur.stage && g.cur.stage !== lastStage) {
      lastStage = mine.lastStage = g.cur.stage;
      dirty = true;
      if (isOpen()) render();
    }
    if (g.stars !== seen.stars) {
      seen.stars = g.stars;
      if (!isOpen()) open("galaxy", true);  // the end of a star: show how it went
    }
    if (dirty) {
      dirty = false;
      store(kKey, JSON.stringify(mine));
      upload.due = true;
      if (isOpen()) render();
    }
  }
  setInterval(poll, 250);

  // ---- the room: post ours, fetch everyone's (online players only) ----
  const sessionKey = Array.from(crypto.getRandomValues(new Uint8Array(12)), (b) => b.toString(16).padStart(2, "0")).join("");
  let room = { players: [] };
  const relayBase = () => {
    const r = window.smgEnv?.SMG_RELAY || params.get("SMG_RELAY");
    return r ? r.trim().replace(/^ws/, "http").replace(/\/?$/, "/") : null;
  };
  const inRoom = () => (relayBase() && roombar.dataset.room && roombar.dataset.you ? { code: roombar.dataset.room, you: roombar.dataset.you } : null);
  let posted = "";
  async function upload() {
    const r = inRoom();
    if (!r) {
      posted = "";  // out of the room: the relay forgot us, post again on rejoining
      return;
    }
    const where = `${r.code}/${r.you}`;
    if (!upload.due && posted === where) return;
    upload.due = false;
    posted = where;
    const res = await fetch(`${relayBase()}board/${where}`, {
      method: "POST",
      body: JSON.stringify({ key: sessionKey, total: mine.total, galaxies: mine.galaxies }),
    }).catch(() => null);
    if (!res || (res.status !== 204 && res.status !== 403)) upload.due = true;  // try again (403: an old id's key; the next join fixes it)
  }
  setInterval(upload, 2000);

  async function fetchRoom() {
    const r = inRoom();
    if (!r) {
      room = { players: [] };
      return;
    }
    const res = await fetch(`${relayBase()}board/${r.code}`).catch(() => null);
    const got = res && res.ok ? await res.json().catch(() => null) : null;
    if (got && Array.isArray(got.players)) room = got;
  }

  // ---- the panel ----
  let tab = store("smg.boardTab") === "total" ? "total" : "galaxy";
  let auto = null;  // the timer while it shows by itself after a star
  const isOpen = () => panel.style.display === "block";
  const esc = (t) => String(t).replace(/[<&"]/g, (c) => ({ "<": "&lt;", "&": "&amp;", '"': "&quot;" })[c]);

  function rows() {
    const r = inRoom();
    const you = r ? Number(r.you) : -1;
    const myName = window.smgEnv?.SMG_NAME || params.get("SMG_NAME") || "You";
    // Ours from this browser (fresher than the relay's copy), the rest from the room.
    const all = room.players.filter((p) => p.id !== you).map((p) => ({ name: p.name, me: false, total: p.total, galaxies: p.galaxies || {} }));
    all.push({ name: myName, me: true, total: mine.total, galaxies: mine.galaxies });
    const pick = (p) => (tab === "total" ? p.total : p.galaxies[lastStage]) || { coins: 0, bits: 0 };
    return all.map((p) => ({ ...p, s: pick(p) })).sort((a, b) => points(b.s) - points(a.s) || (a.me ? -1 : b.me ? 1 : 0));
  }

  function render() {
    for (const t of tabs) t.setAttribute("aria-selected", String(t.dataset.tab === tab));
    const list = rows();
    const title = tab === "total" ? "All galaxies, every run" : lastStage ? `${esc(pretty(lastStage))}, best run` : "No galaxy yet";
    let html = `<p class="bd-sub">${title}</p><table><thead><tr><th></th><th>Player</th><th>Coins</th><th>Star Bits</th></tr></thead><tbody>`;
    list.forEach((p, i) => {
      html += `<tr${p.me ? ' class="bd-me"' : ""}><td>${i + 1}</td><td>${esc(p.name)}</td><td>${p.s.coins}</td><td>${p.s.bits}</td></tr>`;
    });
    html += "</tbody></table>";
    html += inRoom()
      ? `<p class="bd-note">Players in room ${esc(roombar.dataset.room)} now. Scores are what each player picked up themselves.</p>`
      : `<p class="bd-note">Only you: play in a room to see your friends' scores here.</p>`;
    if (body.innerHTML !== html) body.innerHTML = html;
  }

  let refresh = null;
  function open(which, byItself) {
    if (which) tab = which;
    document.getElementById("settings").style.display = "none";
    document.getElementById("gear").setAttribute("aria-expanded", "false");
    panel.style.display = "block";
    btn.setAttribute("aria-expanded", "true");
    render();
    fetchRoom().then(render);
    clearInterval(refresh);
    refresh = setInterval(() => fetchRoom().then(() => isOpen() && render()), 2000);
    clearTimeout(auto);
    auto = null;
    panel.classList.toggle("bd-auto", !!byItself);
    if (byItself) {
      timer.style.animation = "none";
      void timer.offsetWidth;  // restart the bar
      timer.style.animation = "";
      auto = setTimeout(close, kAutoMs);
    }
  }
  function close() {
    panel.style.display = "none";
    btn.setAttribute("aria-expanded", "false");
    clearInterval(refresh);
    clearTimeout(auto);
    auto = null;
  }
  // Opened by itself, a press keeps it open; otherwise it toggles.
  function toggle() {
    if (auto) open();
    else if (isOpen()) close();
    else open();
  }
  window.smgBoard = { open, close, toggle };

  btn.onclick = toggle;
  document.getElementById("boardClose").onclick = close;
  for (const t of tabs) {
    t.onclick = () => {
      tab = t.dataset.tab;
      store("smg.boardTab", tab);
      if (auto) open();  // a tab choice keeps it open
      render();
    };
  }
  document.getElementById("gear").addEventListener("click", close);
  const playing = () => document.body.classList.contains("playing");
  // Tab: the game has no use for it; the page would move the focus.
  addEventListener("keydown", (e) => {
    if (e.code !== "Tab" || !playing() || e.target.closest?.("input, select, textarea")) return;
    e.preventDefault();
    if (!e.repeat) toggle();
  }, true);
  // A click back on the game (the free cursor's way out) closes it, as it closes the settings.
  addEventListener("mousedown", (e) => {
    if (e.target.id === "canvas" && document.body.classList.contains("free-cursor")) close();
  }, true);

  // Gamepad Y (standard mapping button 3): the game maps none of its
  // buttons there (present_web.cpp readGamepad).
  let padDown = false;
  function pollPad() {
    requestAnimationFrame(pollPad);
    if (!playing() || !navigator.getGamepads) return;
    let down = false;
    for (const p of navigator.getGamepads()) {
      if (p && p.connected && p.mapping === "standard" && p.buttons[3]?.pressed) down = true;
    }
    if (down && !padDown) toggle();
    padDown = down;
  }
  requestAnimationFrame(pollPad);
})();
