// In-browser cooker driver (P1-02).  Takes a Dolphin-extracted disc folder
// (the files from listPicked in app.js), plans the jobs the way cook.py's
// plan() does, runs them on a pool of cook-worker.js workers, and writes the
// result into an OPFS directory with a manifest.json identical in format to
// tools/cook/manifest.py's.
"use strict";

// eslint-disable-next-line no-unused-vars
async function cookDisc(files, outDir, onProgress, { movies = false } = {}) {
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
  // types converted, movies skipped unless asked for (cook.py --with-movies),
  // everything else copied.
  const jobs = [], movieJobs = [];
  for (const [p, file] of byPath) {
    if (!p.startsWith("files/")) continue;
    const rel = p.slice(6);
    const ext = (rel.match(/\.[^./]*$/) || [""])[0].toLowerCase();
    if (rel.split("/")[0] === "MovieData") {
      if (movies && ext === ".thp") movieJobs.push({ rel, file, size: file.size });
      continue;
    }
    const mode = ext === ".arc" || ext === ".szs" ? "archive" : ext === ".bcsv" || ext === ".tbl" || ext === ".ast" ? "file" : ext === ".thp" ? null : "copy";
    if (mode) jobs.push({ rel, mode, file, size: file.size });
  }
  // The planets whose collision cook.py rebuilds from the model
  // (collision.FROM_MODEL) take far longer than their size suggests (about
  // 100 s each under Firefox's Pyodide): start them first, then the big
  // archives, so the pool stays busy to the end.
  const kSlow = ["ObjectData/MoonPlanet.arc", "ObjectData/FullMoonPlanet.arc", "ObjectData/HeavensDoorBlackHolePlanet.arc"];
  const weight = (j) => (kSlow.includes(j.rel) ? 1e12 : j.size);
  jobs.sort((a, b) => weight(b) - weight(a));

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
    if (bytes.byteLength) await w.write(bytes); // an empty write never resolved in Firefox 132
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
  const inFlight = (window.smgCookInFlight = new Set()); // for diagnosing a stuck job
  const totalBytes = [...jobs, ...movieJobs].reduce((s, j) => s + j.size, 0);
  let next = 0;
  async function runOn(w) {
    while (next < jobs.length) {
      const j = jobs[next++];
      inFlight.add(j.rel);
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
      inFlight.delete(j.rel);
      done++;
      doneBytes += j.size;
      onProgress?.(doneBytes / totalBytes, `cooking ${done} / ${jobs.length} files`);
    }
  }
  await Promise.all(workers.map(runOn));
  workers.forEach((w) => w.terminate());
  // Movies are hundreds of megabytes each: one at a time, on this thread.
  for (const j of movieJobs) {
    try {
      const bytes = new Uint8Array(await j.file.arrayBuffer());
      cookThp(bytes, j.rel);
      await put("files/" + j.rel, bytes);
      stats.movies = (stats.movies || 0) + 1;
    } catch (e) {
      errors.push(`${j.rel}: ${e.message}`);
    }
    done++;
    doneBytes += j.size;
    onProgress?.(doneBytes / totalBytes, `cooking ${done} / ${jobs.length + movieJobs.length} files`);
  }
  const seconds = (performance.now() - t0) / 1000;
  console.log(`cook: ${jobs.length} disc files in ${seconds.toFixed(1)} s with ${n} workers`);
  console.log("cook: " + Object.entries(stats).sort().map(([k, v]) => `${k} ${v}`).join(", "));
  for (const [msg, count] of notes) console.log(`cook: note (${count}x): ${msg}`);
  for (const e of errors) console.log("cook: ERROR " + e);
  if (errors.length) throw new Error(`${errors.length} files failed to convert (first: ${errors[0]})`);

  // manifest.json last.
  const text = manifestJson([...manifest.keys()].sort().map((path) => ({ path, ...manifest.get(path) })));
  const mh = await outDir.getFileHandle("manifest.json", { create: true });
  const mw = await mh.createWritable();
  await mw.write(text);
  await mw.close();
  return { files: manifest.size, seconds };
}

// manifest.json text in tools/cook/manifest.py's exact format (json.dump
// indent=0), so the same files always give the same text.
function manifestJson(entries) {
  const items = entries.map((e) => `{\n"path": ${JSON.stringify(e.path)},\n"size": ${e.size},\n"sha1": "${e.sha1}"\n}`);
  return `{\n"version": 1,\n"files": [\n${items.join(",\n")}\n]\n}`;
}

// tools/cook/thp.py cook_thp(), in place: the file header, component info,
// frame headers and audio record headers become little-endian; the video and
// ADPCM data are byte streams and stay as they are.
function cookThp(bytes, name) {
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const swap32 = (off, count) => {
    const out = [];
    for (let i = 0; i < count; i++) {
      const x = v.getUint32(off + 4 * i, false);
      v.setUint32(off + 4 * i, x, true);
      out.push(x);
    }
    return out;
  };
  const swap16 = (off, count) => {
    for (let i = 0; i < count; i++) v.setUint16(off + 2 * i, v.getUint16(off + 2 * i, false), true);
  };
  if (v.getUint32(0, false) !== 0x54485000 || v.getUint32(4, false) !== 0x00011000) throw new Error(`${name}: not a version 1.1 THP file`);
  const [, , , , numFrames, firstSize, , compOff, , movieOff] = swap32(4, 11);
  const ncomp = swap32(compOff, 1)[0];
  const kinds = Array.from(bytes.subarray(compOff + 4, compOff + 4 + ncomp));
  let pos = compOff + 0x14, tracks = 1;
  for (const kind of kinds) {
    if (kind === 0) {        // video: xSize, ySize, videoType
      swap32(pos, 3);
      pos += 12;
    } else if (kind === 1) { // audio: channels, frequency, samples, tracks
      tracks = swap32(pos, 4)[3];
      pos += 16;
    } else {
      throw new Error(`${name}: unknown THP component ${kind}`);
    }
  }
  const kAudioHeaderHalfwords = 16 + 16 + 4; // lCoef[8][2], rCoef[8][2], lYn1/2, rYn1/2
  let off = movieOff, size = firstSize;
  for (let f = 0; f < numFrames; f++) {
    const words = swap32(off, 2 + ncomp);
    let data = off + 8 + 4 * ncomp;
    kinds.forEach((kind, i) => {
      if (kind === 1) {
        for (let t = 0; t < tracks; t++) {
          const rec = data + words[2 + i] * t;
          swap32(rec, 2); // offsetNextChannel, sampleSize
          swap16(rec + 8, kAudioHeaderHalfwords);
        }
      }
      data += words[2 + i];
    });
    off += size;
    size = words[0];
  }
  return numFrames;
}
