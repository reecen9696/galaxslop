// Development server for the browser build (no dependencies).
//   SMG_GAME=~/smg/game node web/dev-server.mjs [port]
// Serves web/index.html and build/web/smg.{js,wasm} at /, the cooked game
// folder at /game/ (with HTTP range requests, which the WasmFS fetch backend
// uses), and sends the COOP/COEP headers that SharedArrayBuffer (threads)
// requires.
import { createServer } from "node:http";
import { createReadStream, statSync } from "node:fs";
import { extname, join, normalize, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const repo = resolve(fileURLToPath(import.meta.url), "../..");
const game = process.env.SMG_GAME;
const port = Number(process.argv[2] ?? 8080);
if (!game) {
  console.error("set SMG_GAME to your cooked game folder (tools/cook/cook.py output)");
  process.exit(2);
}
const types = { ".html": "text/html", ".js": "text/javascript", ".wasm": "application/wasm", ".json": "application/json" };

function resolveUrl(url) {
  const path = decodeURIComponent(new URL(url, "http://x").pathname);
  if (path === "/") return join(repo, "web/index.html");
  if (path.startsWith("/game/")) {
    const file = normalize(join(game, path.slice(6)));
    return file.startsWith(resolve(game)) ? file : null;
  }
  for (const dir of ["web", "build/web"]) {
    const file = normalize(join(repo, dir, path));
    if (file.startsWith(join(repo, dir))) {
      try {
        if (statSync(file).isFile()) return file;
      } catch {}
    }
  }
  return null;
}

createServer((req, res) => {
  res.setHeader("Cross-Origin-Opener-Policy", "same-origin");
  res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
  res.setHeader("Cross-Origin-Resource-Policy", "same-origin");
  res.setHeader("Cache-Control", "no-cache");
  const file = resolveUrl(req.url);
  let st;
  try {
    st = file && statSync(file);
  } catch {}
  if (!st || !st.isFile()) {
    res.writeHead(404).end();
    return;
  }
  res.setHeader("Content-Type", types[extname(file)] ?? "application/octet-stream");
  res.setHeader("Accept-Ranges", "bytes");
  const range = /bytes=(\d+)-(\d*)/.exec(req.headers.range ?? "");
  if (range) {
    const start = Number(range[1]);
    const end = range[2] ? Math.min(Number(range[2]), st.size - 1) : st.size - 1;
    res.writeHead(206, { "Content-Range": `bytes ${start}-${end}/${st.size}`, "Content-Length": end - start + 1 });
    if (req.method === "HEAD") return res.end();
    createReadStream(file, { start, end }).pipe(res);
  } else {
    res.writeHead(200, { "Content-Length": st.size });
    if (req.method === "HEAD") return res.end();
    createReadStream(file).pipe(res);
  }
}).listen(port, () => console.log(`http://localhost:${port}/  (game files from ${game})`));
