// Leaderboard (P5-07): the coins and star bits each player picked up
// themselves.  The scores belong to the player's browser, which keeps them
// across sessions and posts them here while it is in a room; the room's board
// lists only the players in the room now.  Memory only: a player who leaves
// is forgotten (Relay's onLeave) and posts again on joining.
//
//   POST /board/<room>/<player id>  text/plain JSON {key, total, galaxies}
//   GET  /board/<room>              {players: [{id, name, total, galaxies}]}
//
// `key` is a random string the page makes per session: the first post for a
// player id binds it, so nobody else in the room can overwrite their scores.
// Scores are whatever the page says (each client counts its own pickups).
import type { IncomingMessage, ServerResponse } from "node:http";
import type { PlayerInfo } from "./protocol.ts";

export interface Score {
  coins: number;
  bits: number;
}

interface Entry {
  key: string;
  total: Score;
  galaxies: Record<string, Score>;
}

const kMaxBody = 32 * 1024;
const kMaxGalaxies = 160;
const kMaxScore = 1e9;

function score(v: unknown): Score | null {
  if (typeof v !== "object" || v === null) return null;
  const { coins, bits } = v as Record<string, unknown>;
  const ok = (n: unknown) => typeof n === "number" && Number.isInteger(n) && n >= 0 && n <= kMaxScore;
  return ok(coins) && ok(bits) ? { coins: coins as number, bits: bits as number } : null;
}

// The posted body, or null when it is not one.
export function parseEntry(text: string): Entry | null {
  let v: unknown;
  try {
    v = JSON.parse(text);
  } catch {
    return null;
  }
  if (typeof v !== "object" || v === null) return null;
  const o = v as Record<string, unknown>;
  if (typeof o.key !== "string" || !/^[A-Za-z0-9_-]{8,64}$/.test(o.key)) return null;
  const total = score(o.total);
  if (!total || typeof o.galaxies !== "object" || o.galaxies === null) return null;
  const galaxies: Record<string, Score> = {};
  const names = Object.keys(o.galaxies);
  if (names.length > kMaxGalaxies) return null;
  for (const name of names) {
    const s = score((o.galaxies as Record<string, unknown>)[name]);
    if (!/^[A-Za-z0-9_]{1,47}$/.test(name) || !s) return null;
    galaxies[name] = s;
  }
  return { key: o.key, total, galaxies };
}

export class Board {
  private readonly rooms = new Map<string, Map<number, Entry>>();
  private readonly members: (code: string) => PlayerInfo[] | null;
  constructor(members: (code: string) => PlayerInfo[] | null) {
    this.members = members;
  }

  // 204 stored, 400 bad body, 403 another key holds this id, 404 no such player.
  post(code: string, id: number, text: string): number {
    const room = code.toUpperCase();
    if (!this.members(room)?.some((p) => p.id === id)) return 404;
    const entry = parseEntry(text);
    if (!entry) return 400;
    let scores = this.rooms.get(room);
    if (!scores) this.rooms.set(room, (scores = new Map()));
    const had = scores.get(id);
    if (had && had.key !== entry.key) return 403;
    scores.set(id, entry);
    return 204;
  }

  get(code: string): { players: { id: number; name: string; total: Score; galaxies: Record<string, Score> }[] } | null {
    const room = code.toUpperCase();
    const members = this.members(room);
    if (!members) return null;
    const scores = this.rooms.get(room);
    const players = [];
    for (const p of members) {
      const e = scores?.get(p.id);
      if (e) players.push({ id: p.id, name: p.name, total: e.total, galaxies: e.galaxies });
    }
    return { players };
  }

  forget(code: string, id: number): void {
    const scores = this.rooms.get(code);
    scores?.delete(id);
    if (scores?.size === 0) this.rooms.delete(code);
  }
}

// The HTTP side; returns false when the request is not for /board.  Any
// origin may ask (the dev page is on another port), with a plain text body
// so the browser sends it without a preflight.
export function boardHandler(board: Board) {
  const headers = { "Cache-Control": "no-store", "Access-Control-Allow-Origin": "*", "Cross-Origin-Resource-Policy": "cross-origin" };
  return (req: IncomingMessage, res: ServerResponse): boolean => {
    const m = /^\/board\/([A-Za-z0-9]{1,16})(?:\/(\d{1,3}))?$/.exec(req.url ?? "");
    if (!m) return false;
    if (req.method === "OPTIONS") {
      res.writeHead(204, { ...headers, "Access-Control-Allow-Methods": "GET, POST", "Access-Control-Allow-Headers": "Content-Type" }).end();
    } else if (req.method === "GET" && !m[2]) {
      const out = board.get(m[1]);
      res.writeHead(out ? 200 : 404, { ...headers, "Content-Type": "application/json" }).end(JSON.stringify(out ?? { error: "no such room" }));
    } else if (req.method === "POST" && m[2]) {
      const chunks: Buffer[] = [];
      let size = 0;
      req.on("data", (c: Buffer) => {
        size += c.length;
        if (size > kMaxBody) {
          res.writeHead(413, headers).end();
          req.destroy();
        } else chunks.push(c);
      });
      req.on("end", () => {
        if (size > kMaxBody) return;
        res.writeHead(board.post(m[1], Number(m[2]), Buffer.concat(chunks).toString("utf8")), headers).end();
      });
    } else {
      res.writeHead(405, headers).end();
    }
    return true;
  };
}
