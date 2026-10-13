// Browser smoke test: boots the web build in a private headless Chrome and
// checks that game frames keep arriving.  No dependencies (Node 22+ has
// WebSocket; Chrome is driven over the DevTools protocol).
//
//   node tools/web/smoke.mjs [--url http://localhost:8080/] [--seconds 20]
//        [--runs N] [--params "PETARI_INPUT=..."] [--shot out.png]
//        [--keys "5000:Space,5200:-Space"]
//        [--import ~/smg/game] [--profile dir] [--eval "js expression"]
//        [--log full-log.txt] [--stacks 0] [--at "2000:document.getElementById('solo').click()"]
//
// The lobby is skipped (?solo=1) unless --params mentions SMG_RELAY or lobby.
//
// By default the page reads the dev server's game folder (?source=server).
// --import feeds the folder to the page's import screen instead (OPFS);
// --profile keeps the Chrome profile, and so OPFS and saves, between runs.
//
// Needs web/dev-server.mjs running.  Exit status 1 if any run stalls (the
// page logs "no new game frame") or never presents.
import { spawn } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const args = Object.fromEntries(
  process.argv.slice(2).reduce((acc, a, i, all) => (a.startsWith("--") ? [...acc, [a.slice(2), all[i + 1]]] : acc), []),
);
const base = args.url ?? "http://localhost:8080/";
const seconds = Number(args.seconds ?? 20);
const runs = Number(args.runs ?? 1);
const chrome =
  process.env.CHROME ??
  (process.platform === "darwin" ? "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" : "google-chrome");

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function launch() {
  const profile = args.profile ?? mkdtempSync(join(tmpdir(), "smg-smoke-"));
  const proc = spawn(chrome, [
    "--headless=new", `--user-data-dir=${profile}`, "--remote-debugging-port=0", "--no-first-run",
    "--ignore-gpu-blocklist", "--enable-unsafe-swiftshader", "--autoplay-policy=no-user-gesture-required", "about:blank",
  ], { stdio: ["ignore", "ignore", "pipe"] });
  const wsUrl = await new Promise((resolve, reject) => {
    let buf = "";
    proc.stderr.on("data", (d) => {
      buf += d;
      const m = /DevTools listening on (ws:\S+)/.exec(buf);
      if (m) resolve(m[1]);
    });
    proc.on("exit", () => reject(new Error("chrome exited: " + buf.slice(-500))));
  });
  return { proc, profile, wsUrl };
}

function cdp(wsUrl) {
  const ws = new WebSocket(wsUrl);
  let id = 0;
  const pending = new Map();
  const listeners = [];
  ws.onmessage = (e) => {
    const msg = JSON.parse(e.data);
    if (msg.id && pending.has(msg.id)) {
      const { resolve, reject } = pending.get(msg.id);
      pending.delete(msg.id);
      msg.error ? reject(new Error(msg.error.message)) : resolve(msg.result);
    } else {
      for (const l of listeners) l(msg);
    }
  };
  const ready = new Promise((r) => (ws.onopen = r));
  return {
    ready,
    send: (method, params = {}, sessionId) =>
      new Promise((resolve, reject) => {
        const n = ++id;
        pending.set(n, { resolve, reject });
        ws.send(JSON.stringify({ id: n, method, params, sessionId }));
      }),
    on: (fn) => listeners.push(fn),
    close: () => ws.close(),
  };
}

const kKeyCodes = { Space: 32, Enter: 13, KeyW: 87, KeyA: 65, KeyS: 83, KeyD: 68, KeyK: 75, KeyC: 67, ArrowUp: 38, ArrowDown: 40, ArrowLeft: 37, ArrowRight: 39, ShiftLeft: 16, Digit1: 49, Digit2: 50 };

async function oneRun(n) {
  const { proc, profile, wsUrl } = await launch();
  const c = cdp(wsUrl);
  await c.ready;
  const { targetId } = await c.send("Target.createTarget", { url: "about:blank" });
  const { sessionId } = await c.send("Target.attachToTarget", { targetId, flatten: true });
  const log = [];
  c.on((m) => {
    if (m.method === "Runtime.exceptionThrown") {
      const d = m.params.exceptionDetails;
      log.push("EXCEPTION" + (m.sessionId === sessionId ? "" : " (worker)") + ": " + (d.exception?.description ?? d.text));
    }
    if (m.sessionId !== sessionId && m.method === "Runtime.consoleAPICalled" && /error|warn/.test(m.params.type)) {
      log.push("worker " + m.params.type + ": " + m.params.args.map((a) => a.value ?? a.description ?? "").join(" "));
    }
    if (m.sessionId === sessionId && m.method === "Runtime.consoleAPICalled") {
      log.push(m.params.args.map((a) => a.value ?? a.description ?? "").join(" "));
    }
  });
  await c.send("Runtime.enable", {}, sessionId);
  await c.send("Page.enable", {}, sessionId);
  // Workers (the game's threads) attach as child sessions; on a stall each
  // one is paused in the debugger to print where it is (wasm keeps function
  // names: --profiling-funcs).
  const workers = new Set();
  const paused = new Map();
  c.on((m) => {
    if (m.method === "Target.attachedToTarget" && m.params.targetInfo.type === "worker") {
      workers.add(m.params.sessionId);
      c.send("Runtime.enable", {}, m.params.sessionId).catch(() => {});
      c.send("Runtime.runIfWaitingForDebugger", {}, m.params.sessionId).catch(() => {});
    }
    if (m.method === "Debugger.paused" && paused.has(m.sessionId)) {
      paused.get(m.sessionId)(m.params.callFrames.map((f) => f.functionName || "?"));
    }
  });
  await c.send("Target.setAutoAttach", { autoAttach: true, waitForDebuggerOnStart: false, flatten: true }, sessionId);
  let stacksDumped = false;
  async function dumpStacks() {
    stacksDumped = true;
    console.log(`stall: pausing ${workers.size} workers for stacks`);
    for (const w of workers) {
      try {
        await c.send("Debugger.enable", {}, w);
        const frames = await Promise.race([
          new Promise((resolve) => {
            paused.set(w, resolve);
            c.send("Debugger.pause", {}, w).catch(() => {});
          }),
          sleep(4000).then(() => "timeout"),
        ]);
        paused.delete(w);
        if (frames === "timeout") {
          console.log("   worker: (did not pause: blocked in a wait or idle)");
        } else if (frames) {
          console.log("   worker: " + frames.slice(0, 14).join(" < "));
          await c.send("Debugger.resume", {}, w).catch(() => {});
        }
      } catch {}
    }
  }
  const source = (args.import || args.profile ? "" : "&source=server") + (/SMG_RELAY|lobby/.test(args.params ?? "") ? "" : "&solo=1");
  const url = base + "?PETARI_TIMER=1" + source + (args.params ? "&" + args.params : "") + `&run=${n}`;
  await c.send("Page.navigate", { url }, sessionId);
  if (args.import) {
    // Wait for the import screen, then hand the folder to its <input webkitdirectory>.
    await c.send("DOM.enable", {}, sessionId);
    for (let i = 0; i < 100; i++) {
      const { result } = await c.send("Runtime.evaluate", { expression: "getComputedStyle(document.getElementById('import')).display" }, sessionId);
      if (result.value === "block") break;
      if (i === 99) throw new Error("no import screen (already imported? use a fresh --profile)");
      await sleep(100);
    }
    const { root } = await c.send("DOM.getDocument", {}, sessionId);
    const { nodeId } = await c.send("DOM.querySelector", { nodeId: root.nodeId, selector: "#pickInput" }, sessionId);
    await c.send("DOM.setFileInputFiles", { nodeId, files: [args.import] }, sessionId);
    const t0 = Date.now();
    for (;;) {
      const { result } = await c.send("Runtime.evaluate", { expression: "document.getElementById('status').textContent" }, sessionId);
      if (!/copying|reading/.test(result.value ?? "") && Date.now() - t0 > 2000) break;
      if (Date.now() - t0 > 600000) throw new Error("import took over 10 min");
      await sleep(500);
    }
    console.log(`import finished in ${((Date.now() - t0) / 1000).toFixed(0)} s`);
  }
  const start = Date.now();
  // --at "1500:js expression;9000:js expression" runs page script at times.
  const ats = (args.at ?? "").split(";").filter(Boolean).map((e) => ({ t: Number(e.slice(0, e.indexOf(":"))), js: e.slice(e.indexOf(":") + 1) }));
  const keys = (args.keys ?? "").split(",").filter(Boolean).map((k) => {
    const [t, name] = k.split(":");
    return { t: Number(t), up: name.startsWith("-"), code: name.replace(/^-/, "") };
  });
  while (Date.now() - start < seconds * 1000) {
    const now = Date.now() - start;
    if (!stacksDumped && args.stacks !== "0" && log.some((l) => l.includes("no new game frame"))) {
      await dumpStacks();
    }
    while (ats.length && ats[0].t <= now) {
      const { result } = await c.send("Runtime.evaluate", { expression: ats.shift().js, returnByValue: true }, sessionId);
      if (result.subtype === "error") console.log("at: " + result.description);
    }
    while (keys.length && keys[0].t <= now) {
      const k = keys.shift();
      await c.send("Input.dispatchKeyEvent", { type: k.up ? "keyUp" : "rawKeyDown", code: k.code, key: k.code, windowsVirtualKeyCode: kKeyCodes[k.code] ?? 0 }, sessionId);
    }
    await sleep(100);
  }
  if (args.eval) {
    const { result } = await c.send("Runtime.evaluate", { expression: args.eval, awaitPromise: true, returnByValue: true }, sessionId);
    console.log("eval:", JSON.stringify(result.value));
  }
  if (args.shot) {
    const { data } = await c.send("Page.captureScreenshot", { format: "png" }, sessionId);
    const file = runs > 1 ? args.shot.replace(/\.png$/, `-${n}.png`) : args.shot;
    writeFileSync(file, Buffer.from(data, "base64"));
  }
  c.close();
  proc.kill("SIGKILL");
  await sleep(300);
  if (!args.profile) rmSync(profile, { recursive: true, force: true });
  if (args.log) writeFileSync(runs > 1 ? args.log.replace(/(\.\w+)?$/, `-${n}$1`) : args.log, log.join("\n") + "\n");
  const stalled = log.find((l) => l.includes("no new game frame"));
  const frames = log.filter((l) => l.includes("display frames")).pop();
  return { log, stalled, frames, ok: !stalled && !!frames };
}

let failed = 0;
for (let n = 1; n <= runs; n++) {
  const r = await oneRun(n);
  if (!r.ok) failed++;
  console.log(`run ${n}: ${r.ok ? "ok" : "FAIL"}  ${r.frames ?? "(no frames logged)"}`);
  if (!r.ok || args.verbose) {
    for (const l of r.log.filter((l) => r.stalled ? /no new|^thread|threads:|EXCEPTION|^worker/.test(l) : true).slice(-60)) console.log("   " + l);
  }
}
console.log(`${runs - failed}/${runs} runs ok`);
process.exit(failed ? 1 : 0);
