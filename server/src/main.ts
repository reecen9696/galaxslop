// The relay as a WebSocket server.  Environment: PORT (default 8787),
// ALLOWED_ORIGINS (comma-separated; unset = any, for local play), ROOMS
// (comma-separated permanent room codes, e.g. ROOMS=PLAY22), STATIC_DIR
// (also serve the web build from there: tools/web/bundle.mjs output), GAME_DIR
// (a cooked game folder the page loads by itself; own machine or LAN only).
import { createServer } from "node:http";
import { WebSocketServer, type WebSocket } from "ws";
import { Relay, type Conn } from "./relay.ts";
import { staticHandler } from "./static.ts";

export function startServer(port: number, log: (m: string) => void = console.log): { close: () => Promise<void>; port: () => number; ready: Promise<void> } {
  const allowed = (process.env.ALLOWED_ORIGINS ?? "")
    .split(",")
    .map((s) => s.trim())
    .filter(Boolean);
  const fixedRooms = (process.env.ROOMS ?? "").split(",").filter((s) => s.trim());
  const relay = new Relay({ log, fixedRooms });
  const serve = process.env.STATIC_DIR ? staticHandler(process.env.STATIC_DIR, process.env.GAME_DIR) : (_req: unknown, res: { writeHead: (n: number) => { end: () => void } }) => res.writeHead(426).end();
  // /healthz: for the host's health check, with or without STATIC_DIR.
  const http = createServer((req, res) => {
    if (req.url === "/healthz") res.writeHead(200, { "Content-Type": "text/plain" }).end("ok\n");
    else serve(req, res);
  });
  const ready = new Promise<void>((resolve, reject) => {
    http.once("listening", resolve);
    http.once("error", reject);
  });
  http.listen(port);
  const wss = new WebSocketServer({
    server: http,
    maxPayload: 4096,
    verifyClient: (info: { origin?: string }) => allowed.length === 0 || !info.origin || allowed.includes(info.origin),
  });
  wss.on("connection", (ws: WebSocket) => {
    ws.binaryType = "nodebuffer";
    const conn: Conn = {
      send: (b) => {
        if (ws.readyState === ws.OPEN) ws.send(b);
      },
      close: () => ws.close(),
    };
    relay.connect(conn);
    ws.on("message", (data: Buffer, isBinary: boolean) => {
      if (isBinary) relay.message(conn, new Uint8Array(data.buffer, data.byteOffset, data.byteLength));
    });
    ws.on("close", () => relay.disconnect(conn));
    ws.on("error", () => relay.disconnect(conn));
  });
  const timer = setInterval(() => relay.tick(), 2000);
  wss.on("error", () => {});  // reported through `ready`
  return {
    ready,
    port: () => {
      const a = http.address();
      return typeof a === "object" && a ? a.port : port;
    },
    close: () =>
      new Promise<void>((resolve) => {
        clearInterval(timer);
        for (const c of wss.clients) c.terminate();
        wss.close(() => http.close(() => resolve()));
      }),
  };
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const port = Number(process.env.PORT ?? 8787);
  const s = startServer(port);
  s.ready.then(
    () => console.log(`relay listening on ws://localhost:${s.port()}` + (process.env.STATIC_DIR ? `, page at http://localhost:${s.port()}/` : "")),
    (e: Error) => {
      console.error(`relay: cannot listen on port ${port}: ${e.message}`);
      process.exit(1);
    },
  );
}
