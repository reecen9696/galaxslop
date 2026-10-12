import { afterEach, beforeEach, describe, expect, it } from "vitest";
import WebSocket from "ws";
import { startServer } from "../src/main.ts";
import { ErrorCode, Msg, PROTO_VERSION, decodeWelcome, encodeHello } from "../src/protocol.ts";
import { MAX_PLAYERS, Relay, type Conn } from "../src/relay.ts";

// A real WebSocket client that collects what it receives.
class TestClient {
  ws: WebSocket;
  inbox: Uint8Array[] = [];
  private waiters: Array<() => void> = [];
  constructor(url: string) {
    this.ws = new WebSocket(url);
    this.ws.binaryType = "nodebuffer";
    this.ws.on("message", (d: Buffer) => {
      this.inbox.push(new Uint8Array(d));
      this.waiters.splice(0).forEach((w) => w());
    });
  }
  open(): Promise<void> {
    return new Promise((res, rej) => {
      this.ws.once("open", () => res());
      this.ws.once("error", rej);
    });
  }
  send(b: Uint8Array): void {
    this.ws.send(b);
  }
  async next(type: number, timeoutMs = 2000): Promise<Uint8Array> {
    const deadline = Date.now() + timeoutMs;
    for (;;) {
      const i = this.inbox.findIndex((m) => m[0] === type);
      if (i >= 0) return this.inbox.splice(i, 1)[0];
      const left = deadline - Date.now();
      if (left <= 0) throw new Error(`no message 0x${type.toString(16)}`);
      await new Promise<void>((res) => {
        const t = setTimeout(res, left);
        this.waiters.push(() => {
          clearTimeout(t);
          res();
        });
      });
    }
  }
  close(): void {
    this.ws.close();
  }
}

function hello(name: string, room = ""): Uint8Array {
  return encodeHello({ protoVer: PROTO_VERSION, name, character: 0, tint: 0, roomCode: room });
}

function state(seq: number): Uint8Array {
  const b = new Uint8Array(1 + 57);
  b[0] = Msg.PlayerState;
  b[1] = seq & 0xff;
  return b;
}

describe("relay over WebSocket", () => {
  let server: ReturnType<typeof startServer>;
  let url: string;
  const clients: TestClient[] = [];
  const connect = async () => {
    const c = new TestClient(url);
    clients.push(c);
    await c.open();
    return c;
  };

  beforeEach(() => {
    server = startServer(0, () => {});
    url = `ws://127.0.0.1:${server.port()}`;
  });
  afterEach(async () => {
    clients.splice(0).forEach((c) => c.close());
    await server.close();
  });

  it("creates a room and welcomes the creator", async () => {
    const a = await connect();
    a.send(hello("Reece"));
    const w = decodeWelcome(await a.next(Msg.Welcome));
    expect(w.you).toBe(1);
    expect(w.roomCode).toMatch(/^[A-Z2-9]{6}$/);
    expect(w.players).toEqual([{ id: 1, name: "Reece", character: 0, tint: 0 }]);
  });

  it("joins by code, announces joins and leaves, relays state to the others only", async () => {
    const a = await connect();
    a.send(hello("A"));
    const { roomCode } = decodeWelcome(await a.next(Msg.Welcome));
    const b = await connect();
    b.send(hello("B", roomCode.toLowerCase()));
    const wb = decodeWelcome(await b.next(Msg.Welcome));
    expect(wb.you).toBe(2);
    expect(wb.players.map((p) => p.name)).toEqual(["A", "B"]);
    expect((await a.next(Msg.PlayerJoined))[1]).toBe(2);

    b.send(state(7));
    const fwd = await a.next(Msg.PlayerState);
    expect(fwd[1]).toBe(2); // sender id prepended
    expect(fwd[2]).toBe(7); // payload follows
    expect(fwd.length).toBe(1 + 1 + 57);
    await new Promise((r) => setTimeout(r, 50));
    expect(b.inbox.some((m) => m[0] === Msg.PlayerState)).toBe(false); // not echoed

    b.close();
    expect((await a.next(Msg.PlayerLeft))[1]).toBe(2);
  });

  it("rejects unknown rooms, full rooms and wrong protocol versions", async () => {
    const a = await connect();
    a.send(hello("A", "ZZZZZZ"));
    expect((await a.next(Msg.Error))[1]).toBe(ErrorCode.RoomNotFound);

    const host = await connect();
    host.send(hello("H"));
    const { roomCode } = decodeWelcome(await host.next(Msg.Welcome));
    for (let i = 1; i < MAX_PLAYERS; i++) {
      const c = await connect();
      c.send(hello(`P${i}`, roomCode));
      await c.next(Msg.Welcome);
    }
    const extra = await connect();
    extra.send(hello("X", roomCode));
    expect((await extra.next(Msg.Error))[1]).toBe(ErrorCode.RoomFull);

    const old = await connect();
    old.send(encodeHello({ protoVer: 99, name: "O", character: 0, tint: 0, roomCode: "" }));
    expect((await old.next(Msg.Error))[1]).toBe(ErrorCode.ProtocolVersion);
  });

  it("answers Ping with Pong carrying the same timestamp", async () => {
    const a = await connect();
    a.send(Uint8Array.of(Msg.Ping, 1, 2, 3, 4));
    expect([...(await a.next(Msg.Pong))]).toEqual([Msg.Pong, 1, 2, 3, 4]);
  });

  it("5 clients at 30 Hz for 2 s: every state reaches every other client", async () => {
    const host = await connect();
    host.send(hello("C0"));
    const { roomCode } = decodeWelcome(await host.next(Msg.Welcome));
    const all = [host];
    for (let i = 1; i < 5; i++) {
      const c = await connect();
      c.send(hello(`C${i}`, roomCode));
      await c.next(Msg.Welcome);
      all.push(c);
    }
    await new Promise((r) => setTimeout(r, 100));
    all.forEach((c) => (c.inbox = []));
    const frames = 60;
    for (let f = 0; f < frames; f++) {
      all.forEach((c) => c.send(state(f)));
      await new Promise((r) => setTimeout(r, 33));
    }
    await new Promise((r) => setTimeout(r, 300));
    for (const c of all) {
      const got = c.inbox.filter((m) => m[0] === Msg.PlayerState).length;
      expect(got).toBe(frames * 4);
    }
  }, 10_000);
});

describe("relay logic", () => {
  it("drops clients silent for 15 s and rate-limits beyond 60 messages a second", () => {
    let t = 0;
    const relay = new Relay({ now: () => t });
    const sent: Uint8Array[][] = [[], []];
    const closed = [false, false];
    const conns: Conn[] = [0, 1].map((i) => ({ send: (b) => sent[i].push(b), close: () => (closed[i] = true) }));
    conns.forEach((c) => relay.connect(c));
    relay.message(conns[0], hello("A"));
    const code = decodeWelcome(sent[0][0]).roomCode;
    relay.message(conns[1], hello("B", code));
    sent[0].length = 0;

    for (let i = 0; i < 100; i++) relay.message(conns[1], state(i));
    expect(sent[0].filter((m) => m[0] === Msg.PlayerState).length).toBe(60 - 1); // the Hello used one of B's 60

    t = 16_000;
    relay.message(conns[0], Uint8Array.of(Msg.Ping, 0, 0, 0, 0));
    relay.tick();
    expect(closed[1]).toBe(true);
    expect(sent[0].some((m) => m[0] === Msg.PlayerLeft && m[1] === 2)).toBe(true);
    expect(closed[0]).toBe(false);
  });
});
