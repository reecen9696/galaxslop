// Problem reports: the page's Settings > "Send log" posts its recent log
// here and shows the player a short code; GET /report/<code> reads it back.
// Kept in memory only (the newest kMaxReports), so a restart forgets them.
import { randomBytes } from "node:crypto";
import type { IncomingMessage, ServerResponse } from "node:http";

const kMaxBytes = 1 << 20;
const kMaxReports = 50;

export function reportHandler(log: (m: string) => void) {
  const reports = new Map<string, string>();
  // Returns false when the request is not for /report.
  return (req: IncomingMessage, res: ServerResponse): boolean => {
    const url = req.url ?? "";
    const get = /^\/report\/([a-z0-9]{6})$/.exec(url);
    if (get && req.method === "GET") {
      const text = reports.get(get[1]);
      res.writeHead(text ? 200 : 404, { "Content-Type": "text/plain; charset=utf-8", "Cache-Control": "no-store" });
      res.end(text ?? "no such report\n");
      return true;
    }
    if (url !== "/report" || req.method !== "POST") return false;
    const chunks: Buffer[] = [];
    let size = 0;
    req.on("data", (c: Buffer) => {
      size += c.length;
      if (size > kMaxBytes) {
        res.writeHead(413).end();
        req.destroy();
      } else chunks.push(c);
    });
    req.on("end", () => {
      if (size > kMaxBytes) return;
      const code = randomBytes(6).toString("base64url").toLowerCase().replace(/[^a-z0-9]/g, "0").slice(0, 6);
      reports.set(code, Buffer.concat(chunks).toString("utf8"));
      while (reports.size > kMaxReports) reports.delete(reports.keys().next().value as string);
      log(`report ${code}: ${size} bytes`);
      res.writeHead(200, { "Content-Type": "application/json", "Cache-Control": "no-store" }).end(JSON.stringify({ code }));
    });
    return true;
  };
}
