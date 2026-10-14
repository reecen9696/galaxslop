import { afterEach, beforeEach, describe, expect, it } from "vitest";
import WebSocket from "ws";
import { parseEntry } from "../src/board.ts";
import { startServer } from "../src/main.ts";
import { Msg, PROTO_VERSION, decodeWelcome, encodeHello } from "../src/protocol.ts";

// Joins a room over the real WebSocket; resolves with the Welcome.
function join(url: string, name: string, room = ""): Promise<{ ws: WebSocket; you: number; roomCode: string }> {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(url);
    ws.binaryType = "nodebuffer";
    ws.on("error", reject);
    ws.on("open", () => ws.send(encodeHello({ protoVer: PROTO_VERSION, name, character: 0, tint: 0, roomCode: room })));
    ws.on("message", (d: Buffer) => {
      if (d[0] !== Msg.Welcome) return;
      const w = decodeWelcome(new Uint8Array(d));
      resolve({ ws, you: w.you, roomCode: w.roomCode });
    });
  });
}

const scores = (key: string, coins: number, bits: number) =>
  JSON.stringify({ key, total: { coins, bits }, galaxies: { EggStarGalaxy: { coins, bits } } });

describe("leaderboard (P5-07)", () => {
  let server: ReturnType<typeof startServer>;
  let base: string;
  const sockets: WebSocket[] = [];
  beforeEach(() => {
    server = startServer(0, () => {});
    base = `127.0.0.1:${server.port()}`;
  });
  afterEach(async () => {
    sockets.splice(0).forEach((s) => s.close());
    await server.close();
  });
  const post = (room: string, id: number, body: string) => fetch(`http://${base}/board/${room}/${id}`, { method: "POST", body });
  const board = async (room: string) => (await fetch(`http://${base}/board/${room}`)).json();

  it("lists the scores of the players online in the room, and forgets those who leave", async () => {
    const a = await join(`ws://${base}`, "Reece");
    const b = await join(`ws://${base}`, "Sam", a.roomCode);
    sockets.push(a.ws, b.ws);
    expect((await post(a.roomCode, a.you, scores("key-of-reece", 12, 340))).status).toBe(204);
    expect((await post(a.roomCode, b.you, scores("key-of-sam-1", 30, 5))).status).toBe(204);
    const r = await fetch(`http://${base}/board/${a.roomCode}`);
    expect(r.headers.get("access-control-allow-origin")).toBe("*");
    const got = await r.json();
    expect(got.players.map((p: { name: string; total: unknown }) => [p.name, p.total])).toEqual([
      ["Reece", { coins: 12, bits: 340 }],
      ["Sam", { coins: 30, bits: 5 }],
    ]);
    expect(got.players[0].galaxies.EggStarGalaxy).toEqual({ coins: 12, bits: 340 });

    b.ws.close();
    for (let i = 0; i < 50 && (await board(a.roomCode)).players.length > 1; i++) await new Promise((res) => setTimeout(res, 20));
    expect((await board(a.roomCode)).players.map((p: { name: string }) => p.name)).toEqual(["Reece"]);
  });

  it("refuses another key for a taken id, unknown players and bad bodies", async () => {
    const a = await join(`ws://${base}`, "Reece");
    sockets.push(a.ws);
    expect((await post(a.roomCode, a.you, scores("key-of-reece", 1, 1))).status).toBe(204);
    expect((await post(a.roomCode, a.you, scores("someone-else", 999, 999))).status).toBe(403);
    expect((await post(a.roomCode, a.you, scores("key-of-reece", 2, 2))).status).toBe(204);
    expect((await post(a.roomCode, 9, scores("key-of-nine", 1, 1))).status).toBe(404);
    expect((await post("NOROOM", 1, scores("key-of-reece", 1, 1))).status).toBe(404);
    expect((await post(a.roomCode, a.you, "not json")).status).toBe(400);
    expect((await board(a.roomCode)).players[0].total).toEqual({ coins: 2, bits: 2 });
    expect((await fetch(`http://${base}/board/NOROOM`)).status).toBe(404);
  });

  it("validates scores and galaxy names", () => {
    expect(parseEntry(scores("abcdefgh", 1, 2))).not.toBeNull();
    expect(parseEntry(scores("short", 1, 2))).toBeNull();
    expect(parseEntry(scores("abcdefgh", -1, 2))).toBeNull();
    expect(parseEntry(scores("abcdefgh", 1.5, 2))).toBeNull();
    expect(parseEntry(JSON.stringify({ key: "abcdefgh", total: { coins: 1, bits: 1 }, galaxies: { "../x": { coins: 1, bits: 1 } } }))).toBeNull();
  });
});
