// In-browser cooker worker (P1-02): runs tools/cook/cook.py itself under
// Pyodide, one disc file per job, so the browser output is byte-identical to
// the command line's.  Yaz0 is decoded in JS (cook.py's C decoder cannot load
// here and the pure-Python fallback is far too slow).
//
// Messages in:  {type: "init", pyBase}            -> {type: "ready"}
//               {type: "job", id, rel, mode, data} -> {type: "done", id, out, stats, notes, error}
//               {type: "dol", data}                -> {type: "dol", files: {name: bytes}, errors}
"use strict";

const PYODIDE = "https://cdn.jsdelivr.net/pyodide/v0.26.4/full/";
importScripts(PYODIDE + "pyodide.js");

const kModules = ["cook", "bcsv", "bmg", "collision", "dol_data", "j3d", "jaudio", "jpa", "kcl", "lyt", "manifest",
  "rarc_swap", "thp", "util", "validate", "rarc"];

// Yaz0 (Nintendo's LZ77 variant): 16-byte header, then groups of 8 chunks
// each flagged literal (1) or back-reference (0).
self.smgYaz0 = function (src) {
  const size = ((src[4] << 24) | (src[5] << 16) | (src[6] << 8) | src[7]) >>> 0;
  const out = new Uint8Array(size);
  let s = 16, d = 0;
  while (d < size) {
    const code = src[s++];
    for (let bit = 0x80; bit && d < size; bit >>= 1) {
      if (code & bit) {
        out[d++] = src[s++];
      } else {
        const b1 = src[s], b2 = src[s + 1];
        s += 2;
        let from = d - (((b1 & 0x0f) << 8) | b2) - 1;
        let n = b1 >> 4;
        n = n === 0 ? src[s++] + 0x12 : n + 2;
        if (from < 0) throw new Error("bad Yaz0 stream");
        while (n--) out[d++] = out[from++];
      }
    }
  }
  return out;
};

let py, cook;

async function init(pyBase) {
  py = await loadPyodide({ indexURL: PYODIDE });
  py.FS.mkdirTree("/cook");
  for (const m of kModules) {
    const r = await fetch(`${pyBase}${m}.py`);
    if (!r.ok) throw new Error(`cannot fetch ${m}.py (${r.status})`);
    py.FS.writeFile(`/cook/${m}.py`, new Uint8Array(await r.arrayBuffer()));
  }
  py.runPython(`
import sys, types
sys.path.insert(0, "/cook")
# rarc.py builds its C Yaz0 decoder with a subprocess on first import.
sys.modules["subprocess"] = types.SimpleNamespace(run=lambda *a, **k: None)
import cook
from js import smgYaz0
from pyodide.ffi import to_js

def _yaz0(data):
    if bytes(data[:4]) != b"Yaz0":
        return data
    return smgYaz0(to_js(memoryview(bytes(data)))).to_bytes()

cook.yaz0_decompress = _yaz0
import rarc
rarc.yaz0_decompress = _yaz0
`);
  cook = py.globals.get("cook");
}

function job({ id, rel, mode, data }) {
  const src = "/in/files/" + rel, dst = "/out/files/" + rel;
  py.FS.mkdirTree(src.slice(0, src.lastIndexOf("/")));
  py.FS.writeFile(src, new Uint8Array(data));
  const result = cook.process(py.toPy([src, dst, mode]));
  const [stats, notes, error] = result.toJs({ dict_converter: Object.fromEntries });
  result.destroy();
  let out = null;
  try {
    out = py.FS.readFile(dst);
    py.FS.unlink(dst);
  } catch {}
  py.FS.unlink(src);
  const statsObj = stats instanceof Map ? Object.fromEntries(stats) : stats;
  postMessage({ type: "done", id, out, stats: statsObj, notes: notes ?? [], error: error ?? null }, out ? [out.buffer] : []);
}

function dol({ data }) {
  py.FS.mkdirTree("/in/sys");
  py.FS.mkdirTree("/out/sys");
  py.FS.writeFile("/in/sys/main.dol", new Uint8Array(data));
  const errors = cook.cook_dol_data("/in", "/out").toJs();
  const files = {};
  for (const name of py.FS.readdir("/out/sys")) {
    if (name !== "." && name !== "..") files[name] = py.FS.readFile("/out/sys/" + name);
  }
  postMessage({ type: "dol", files, errors });
}

onmessage = async (e) => {
  const m = e.data;
  try {
    if (m.type === "init") {
      await init(m.pyBase);
      postMessage({ type: "ready" });
    } else if (m.type === "job") {
      job(m);
    } else if (m.type === "dol") {
      dol(m);
    }
  } catch (err) {
    postMessage({ type: m.type === "job" ? "done" : "error", id: m.id, out: null, stats: {}, notes: [], error: String(err && err.message || err) });
  }
};
