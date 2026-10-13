// Static hosting for the web build (P3-05): when STATIC_DIR is set the relay
// also serves the page, so one process (one container) is the whole site and
// the page reaches the relay on its own origin.  Sends the COOP/COEP headers
// the game's threads need.  The game data is never here: players import
// their own disc in the page.
import { createReadStream, statSync } from "node:fs";
import type { IncomingMessage, ServerResponse } from "node:http";
import { extname, join, normalize, resolve } from "node:path";

const kTypes: Record<string, string> = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript",
  ".mjs": "text/javascript",
  ".wasm": "application/wasm",
  ".json": "application/json",
  ".py": "text/plain; charset=utf-8",
  ".css": "text/css",
  ".png": "image/png",
  ".svg": "image/svg+xml",
  ".woff2": "font/woff2",
  ".mp3": "audio/mpeg",
  ".txt": "text/plain; charset=utf-8",
};

// gameDir (GAME_DIR): a cooked game folder (tools/cook/cook.py output) the
// page loads into its storage by itself, so nobody picks a folder.  Only for
// a server on your own machine or LAN: it hands the game to anyone who can
// reach it.
export function staticHandler(dir: string, gameDir?: string) {
  const root = resolve(dir);
  const gameRoot = gameDir ? resolve(gameDir) : null;
  return (req: IncomingMessage, res: ServerResponse) => {
    res.setHeader("Cross-Origin-Opener-Policy", "same-origin");
    res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
    res.setHeader("Cross-Origin-Resource-Policy", "same-origin");
    res.setHeader("X-Content-Type-Options", "nosniff");
    let path: string;
    try {
      path = decodeURIComponent(new URL(req.url ?? "/", "http://x").pathname);
    } catch {
      res.writeHead(400).end();
      return;
    }
    if (path === "/config.json") {
      // Tells the page the relay is on this origin (web/app.js).
      res.writeHead(200, { "Content-Type": "application/json", "Cache-Control": "no-cache" });
      res.end(JSON.stringify({ relay: "same-origin", game: gameRoot ? "game/" : null }));
      return;
    }
    const inGame = gameRoot !== null && path.startsWith("/game/");
    const base = inGame ? (gameRoot as string) : root;
    const file = normalize(join(base, inGame ? path.slice(6) : path === "/" ? "index.html" : path));
    let size = -1;
    try {
      const st = statSync(file);
      if (st.isFile() && file.startsWith(base + "/")) size = st.size;
    } catch {}
    if (size < 0 || req.method !== "GET" && req.method !== "HEAD") {
      res.writeHead(404).end();
      return;
    }
    const type = kTypes[extname(file)] ?? "application/octet-stream";
    const range = /^bytes=(\d+)-(\d*)$/.exec(String(req.headers.range ?? ""));
    if (range && Number(range[1]) < size) {
      const start = Number(range[1]);
      const end = range[2] ? Math.min(Number(range[2]), size - 1) : size - 1;
      res.writeHead(206, { "Content-Type": type, "Content-Length": end - start + 1, "Content-Range": `bytes ${start}-${end}/${size}`, "Accept-Ranges": "bytes" });
      if (req.method === "HEAD") res.end();
      else createReadStream(file, { start, end }).pipe(res);
      return;
    }
    // The big wasm changes per release under the same name: revalidate.
    res.writeHead(200, { "Content-Type": type, "Content-Length": size, "Cache-Control": "no-cache", "Accept-Ranges": "bytes" });
    if (req.method === "HEAD") res.end();
    else createReadStream(file).pipe(res);
  };
}
