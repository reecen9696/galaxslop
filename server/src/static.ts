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
};

export function staticHandler(dir: string) {
  const root = resolve(dir);
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
      res.end(JSON.stringify({ relay: "same-origin" }));
      return;
    }
    const file = normalize(join(root, path === "/" ? "index.html" : path));
    let size = -1;
    try {
      const st = statSync(file);
      if (st.isFile() && (file === root || file.startsWith(root + "/"))) size = st.size;
    } catch {}
    if (size < 0 || req.method !== "GET" && req.method !== "HEAD") {
      res.writeHead(404).end();
      return;
    }
    // The big wasm changes per release under the same name: revalidate.
    res.writeHead(200, { "Content-Type": kTypes[extname(file)] ?? "application/octet-stream", "Content-Length": size, "Cache-Control": "no-cache" });
    if (req.method === "HEAD") res.end();
    else createReadStream(file).pipe(res);
  };
}
