// In-browser cooker driver (P1-02).  Takes a Dolphin-extracted disc folder
// (the files from listPicked in app.js), plans the jobs the way cook.py's
// plan() does, runs them on a pool of cook-worker.js workers, and writes the
// result into an OPFS directory with a manifest.json identical in format to
// tools/cook/manifest.py's.
"use strict";

// eslint-disable-next-line no-unused-vars
async function cookDisc(files, outDir, onProgress) {
  // The game partition holds sys/ and files/; Dolphin may put it under DATA/.
  let root = "";
  if (!files.some((f) => f.path === "sys/fst.bin")) {
    const inner = files.find((f) => /(^|\/)sys\/fst\.bin$/.test(f.path));
    if (!inner) throw new Error("no sys/fst.bin: pick the folder Dolphin extracted the disc to");
    root = inner.path.slice(0, -"sys/fst.bin".length);
  }
  const byPath = new Map();
  for (const f of files) if (f.path.startsWith(root)) byPath.set(f.path.slice(root.length), f.file);
  if (!byPath.has("sys/main.dol")) throw new Error("sys/main.dol is missing: extract the whole disc (sys/ and files/)");

  // cook.py plan(): archives are decompressed and converted, a few file
  // types converted, movies skipped (off by default), everything else copied.
  const jobs = [];
  for (const [p, file] of byPath) {
    if (!p.startsWith("files/")) continue;
    const rel = p.slice(6);
    if (rel.split("/")[0] === "MovieData") continue;
    const ext = (rel.match(/\.[^./]*$/) || [""])[0].toLowerCase();
    const mode = ext === ".arc" || ext === ".szs" ? "archive" : ext === ".bcsv" || ext === ".tbl" || ext === ".ast" ? "file" : ext === ".thp" ? null : "copy";
    if (mode) jobs.push({ rel, mode, file, size: file.size });
  }
  jobs.sort((a, b) => b.size - a.size); // big archives first keeps the pool busy at the end

  const manifest = new Map(); // path -> {size, sha1}
  const dirs = new Map([["", outDir]]);
  async function dirFor(path) {
    let key = "", dir = outDir;
    for (const part of path.split("/").slice(0, -1)) {
      key += part + "/";
      if (!dirs.has(key)) dirs.set(key, await dir.getDirectoryHandle(part, { create: true }));
      dir = dirs.get(key);
    }
    return dir;
  }
  async function sha1(bytes) {
    const h = new Uint8Array(await crypto.subtle.digest("SHA-1", bytes));
    return Array.from(h, (b) => b.toString(16).padStart(2, "0")).join("");
  }
  async function put(path, bytes) {
    const fh = await (await dirFor(path)).getFileHandle(path.split("/").pop(), { create: true });
    const w = await fh.createWritable();
    await w.write(bytes);
    await w.close();
    manifest.set(path, { size: bytes.byteLength, sha1: await sha1(bytes) });
  }

  const n = Math.max(2, Math.min(6, (navigator.hardwareConcurrency || 4) - 1));
  const workers = await Promise.all(Array.from({ length: n }, () => new Promise((resolve, reject) => {
    const w = new Worker("cook/cook-worker.js");
    w.onmessage = (e) => (e.data.type === "ready" ? resolve(w) : reject(new Error(e.data.error)));
    w.onerror = (e) => reject(new Error(e.message));
    w.postMessage({ type: "init", pyBase: new URL("pycook/", location.href).href });
  })));

  const t0 = performance.now();
  const errors = [];
  const stats = {};
  const notes = new Map();

  // sys/: fst.bin copied, main.dol's data (dol_data.py) written beside it.
  await put("sys/fst.bin", new Uint8Array(await byPath.get("sys/fst.bin").arrayBuffer()));
  const dolResult = await new Promise((resolve) => {
    workers[0].onmessage = (e) => resolve(e.data);
    byPath.get("sys/main.dol").arrayBuffer().then((data) => workers[0].postMessage({ type: "dol", data }, [data]));
  });
  if (dolResult.type !== "dol") throw new Error("main.dol: " + dolResult.error);
  errors.push(...(dolResult.errors || []));
  for (const [name, bytes] of Object.entries(dolResult.files)) await put("sys/" + name, bytes);

  let done = 0, doneBytes = 0;
  const totalBytes = jobs.reduce((s, j) => s + j.size, 0);
  let next = 0;
  async function runOn(w) {
    while (next < jobs.length) {
      const j = jobs[next++];
      const data = await j.file.arrayBuffer();
      let out;
      if (j.mode === "copy") {
        out = new Uint8Array(data);
        stats.copied = (stats.copied || 0) + 1;
      } else {
        const r = await new Promise((resolve) => {
          w.onmessage = (e) => resolve(e.data);
          w.postMessage({ type: "job", id: next, rel: j.rel, mode: j.mode, data }, [data]);
        });
        if (r.error) errors.push(r.error);
        for (const [k, v] of Object.entries(r.stats || {})) stats[k] = (stats[k] || 0) + v;
        for (const msg of r.notes || []) notes.set(msg, (notes.get(msg) || 0) + 1);
        out = r.out;
      }
      if (out) await put("files/" + j.rel, out);
      done++;
      doneBytes += j.size;
      onProgress?.(doneBytes / totalBytes, `cooking ${done} / ${jobs.length} files`);
    }
  }
  await Promise.all(workers.map(runOn));
  workers.forEach((w) => w.terminate());
  const seconds = (performance.now() - t0) / 1000;
  console.log(`cook: ${jobs.length} disc files in ${seconds.toFixed(1)} s with ${n} workers`);
  console.log("cook: " + Object.entries(stats).sort().map(([k, v]) => `${k} ${v}`).join(", "));
  for (const [msg, count] of notes) console.log(`cook: note (${count}x): ${msg}`);
  for (const e of errors) console.log("cook: ERROR " + e);
  if (errors.length) throw new Error(`${errors.length} files failed to convert (first: ${errors[0]})`);

  // manifest.json last, in manifest.py's exact format (json.dump indent=0).
  const entries = [...manifest.keys()].sort().map((p) => `{\n"path": ${JSON.stringify(p)},\n"size": ${manifest.get(p).size},\n"sha1": "${manifest.get(p).sha1}"\n}`);
  const text = `{\n"version": 1,\n"files": [\n${entries.join(",\n")}\n]\n}`;
  const mh = await outDir.getFileHandle("manifest.json", { create: true });
  const mw = await mh.createWritable();
  await mw.write(text);
  await mw.close();
  return { files: manifest.size, seconds };
}
