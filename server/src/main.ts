// The relay as a WebSocket server.  PORT (default 8787) and ALLOWED_ORIGINS
// (comma-separated; unset = any, for local play) come from the environment.
import { WebSocketServer, type WebSocket } from "ws";
import { Relay, type Conn } from "./relay.ts";

export function startServer(port: number, log: (m: string) => void = console.log): { close: () => Promise<void>; port: () => number } {
  const allowed = (process.env.ALLOWED_ORIGINS ?? "")
    .split(",")
    .map((s) => s.trim())
    .filter(Boolean);
  const relay = new Relay({ log });
  const wss = new WebSocketServer({
    port,
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
  return {
    port: () => {
      const a = wss.address();
      return typeof a === "object" && a ? a.port : port;
    },
    close: () =>
      new Promise<void>((resolve) => {
        clearInterval(timer);
        for (const c of wss.clients) c.terminate();
        wss.close(() => resolve());
      }),
  };
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const port = Number(process.env.PORT ?? 8787);
  const s = startServer(port);
  console.log(`relay listening on ws://localhost:${s.port()}`);
}
