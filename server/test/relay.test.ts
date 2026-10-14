import { afterEach, beforeEach, describe, expect, it } from "vitest";
import WebSocket from "ws";
import { startServer } from "../src/main.ts";
import {
  ErrorCode,
  Msg,
  PROTO_VERSION,
  decodeProgressLog,
  decodeWelcome,
  encodeHello,
  encodeProgressEvent,
} from "../src/protocol.ts";
import { MAX_PLAYERS, RATE_LIMIT_PER_S, RESEND_MS, Relay, type Conn } from "../src/relay.ts";

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
  it("fixed rooms can be joined by code from the start", () => {
    const relay = new Relay({ fixedRooms: ["play22"] });
    const sent: Uint8Array[] = [];
    const conn: Conn = { send: (b) => sent.push(b), close: () => {} };
    relay.connect(conn);
    relay.message(conn, hello("A", "PLAY22"));
    expect(decodeWelcome(sent[0]).roomCode).toBe("PLAY22");
  });

  it("drops clients silent for 15 s and rate-limits beyond RATE_LIMIT_PER_S messages a second", () => {
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

    for (let i = 0; i < 200; i++) relay.message(conns[1], state(i));
    expect(sent[0].filter((m) => m[0] === Msg.PlayerState).length).toBe(RATE_LIMIT_PER_S - 1); // the Hello used one of them

    t = 16_000;
    relay.message(conns[0], Uint8Array.of(Msg.Ping, 0, 0, 0, 0));
    relay.tick();
    expect(closed[1]).toBe(true);
    expect(sent[0].some((m) => m[0] === Msg.PlayerLeft && m[1] === 2)).toBe(true);
    expect(closed[0]).toBe(false);
  });
});

describe("contact (D-020)", () => {
  it("forwards a Contact to its target only, with the sender's id", () => {
    const relay = new Relay({ fixedRooms: ["ROOM22"] });
    const inbox: Uint8Array[][] = [[], [], []];
    const conns: Conn[] = [0, 1, 2].map((i) => ({ send: (b) => inbox[i].push(b), close: () => {} }));
    conns.forEach((c, i) => {
      relay.connect(c);
      relay.message(c, hello("P" + i, "ROOM22"));
    });
    inbox.forEach((m) => (m.length = 0));
    // Player 1 tramples player 3.
    relay.message(conns[0], Uint8Array.of(Msg.Contact, 3, 1, 0, 0x3c, 0, 0, 0, 0));
    expect(inbox[1].length).toBe(0);
    expect(inbox[2].length).toBe(1);
    expect(Array.from(inbox[2][0])).toEqual([Msg.Contact, 1, 1, 0, 0x3c, 0, 0, 0, 0]);
    // Malformed (wrong size) and self-targeted contacts are dropped.
    relay.message(conns[0], Uint8Array.of(Msg.Contact, 3, 1));
    relay.message(conns[0], Uint8Array.of(Msg.Contact, 1, 1, 0, 0, 0, 0, 0, 0));
    expect(inbox[2].length).toBe(1);
    expect(inbox[0].length).toBe(0);
  });
});

describe("world events", () => {
  it("forwards a WorldEvent to everyone else in the room", () => {
    const relay = new Relay({ fixedRooms: ["ROOM22"] });
    const inbox: Uint8Array[][] = [[], [], []];
    const conns: Conn[] = [0, 1, 2].map((i) => ({ send: (b) => inbox[i].push(b), close: () => {} }));
    conns.forEach((c, i) => {
      relay.connect(c);
      relay.message(c, hello("P" + i, "ROOM22"));
    });
    inbox.forEach((m) => (m.length = 0));
    const ev = new Uint8Array(35);
    ev.set([Msg.WorldEvent, 1, 1, 2, 3, 4, 1, 9, 8, 7, 6]);
    relay.message(conns[0], ev);
    expect(inbox[0].length).toBe(0);
    expect(Array.from(inbox[1][0])).toEqual(Array.from(ev));
    expect(Array.from(inbox[2][0])).toEqual(Array.from(ev));
    relay.message(conns[0], Uint8Array.of(Msg.WorldEvent, 1, 2));
    expect(inbox[1].length).toBe(1);
  });

  it("forwards an ActorState from its sender only", () => {
    const relay = new Relay({ fixedRooms: ["ROOM22"] });
    const inbox: Uint8Array[][] = [[], []];
    const conns: Conn[] = [0, 1].map((i) => ({ send: (b) => inbox[i].push(b), close: () => {} }));
    conns.forEach((c, i) => {
      relay.connect(c);
      relay.message(c, hello("P" + i, "ROOM22"));
    });
    inbox.forEach((m) => (m.length = 0));
    const st = new Uint8Array(12 + 32);
    st.set([Msg.ActorState, 1, 1, 2, 3, 4, 1, 0, 0, 0, 0, 1]);
    relay.message(conns[0], st);
    expect(Array.from(inbox[1][0])).toEqual(Array.from(st));
    st[1] = 2; // claims to be player 2
    relay.message(conns[0], st);
    expect(inbox[1].length).toBe(1);
  });
});

describe("progress log (P3-01)", () => {
  function setup() {
    let t = 0;
    const relay = new Relay({ now: () => t, fixedRooms: ["ROOM22"] });
    const inbox: Uint8Array[][] = [];
    const conns: Conn[] = [];
    const join = (name: string) => {
      const i = conns.length;
      inbox.push([]);
      const c: Conn = { send: (b) => inbox[i].push(b), close: () => {} };
      conns.push(c);
      relay.connect(c);
      relay.message(c, hello(name, "ROOM22"));
      return i;
    };
    const logs = (i: number) => inbox[i].filter((m) => m[0] === Msg.ProgressLog).map(decodeProgressLog);
    const ack = (i: number, seq: number) => {
      const b = new Uint8Array(5);
      b[0] = Msg.ProgressAck;
      new DataView(b.buffer).setUint32(1, seq, true);
      relay.message(conns[i], b);
    };
    return { relay, conns, inbox, join, logs, ack, advance: (ms: number) => (t += ms) };
  }
  const star = (arg: number) => encodeProgressEvent({ kind: 1, stageHash: 0xe15ee5b8, scenario: 1, arg });

  it("appends events, broadcasts them to everyone including the sender, and dedups by what happened", () => {
    const { relay, conns, join, logs } = setup();
    const a = join("A");
    const b = join("B");
    relay.message(conns[a], star(0));
    relay.message(conns[b], star(0)); // same star, other player: not a new event
    relay.message(conns[b], star(1));
    expect(relay.roomLog("ROOM22").map((e) => [e.seq, e.origin, e.arg])).toEqual([
      [0, 1, 0],
      [1, 2, 1],
    ]);
    const seenByA = logs(a).flatMap((l) => l.events.map((e) => e.seq));
    expect(new Set(seenByA)).toEqual(new Set([0, 1]));
  });

  it("sends the whole log to a late joiner", () => {
    const { relay, conns, join, logs } = setup();
    const a = join("A");
    relay.message(conns[a], star(0));
    relay.message(conns[a], star(1));
    relay.message(conns[a], star(2));
    const late = join("Late");
    const first = logs(late)[0];
    expect(first.fromSeq).toBe(0);
    expect(first.events.map((e) => e.arg)).toEqual([0, 1, 2]);
  });

  it("resends from the first unacknowledged entry every 2 s until acked", () => {
    const { relay, conns, join, logs, ack, advance, inbox } = setup();
    const a = join("A");
    const b = join("B");
    relay.message(conns[a], star(0));
    relay.message(conns[a], star(1));
    ack(a, 1);
    ack(b, 0);
    inbox[a].length = 0;
    inbox[b].length = 0;
    advance(RESEND_MS);
    relay.tick();
    expect(logs(a)).toEqual([]); // A has everything
    const resent = logs(b);
    expect(resent.length).toBe(1);
    expect(resent[0].fromSeq).toBe(1);
    expect(resent[0].events.map((e) => e.seq)).toEqual([1]);
    ack(b, 1);
    inbox[b].length = 0;
    advance(RESEND_MS);
    relay.tick();
    expect(logs(b)).toEqual([]);
  });
});

describe("host snapshot (P3-07)", () => {
  function setup() {
    const relay = new Relay({ fixedRooms: ["ROOM22"] });
    const inbox: Uint8Array[][] = [];
    const conns: Conn[] = [];
    const join = (name: string) => {
      const i = conns.length;
      inbox.push([]);
      const c: Conn = { send: (b) => inbox[i].push(b), close: () => {} };
      conns.push(c);
      relay.connect(c);
      relay.message(c, hello(name, "ROOM22"));
      return i;
    };
    const snaps = (i: number) => inbox[i].filter((m) => m[0] === Msg.ProgressSnapshot);
    const offer = (i: number, fill: number, len = 32) => {
      const b = new Uint8Array(3 + len);
      b[0] = Msg.ProgressSnapshot;
      b[1] = len & 0xff;
      b[2] = len >> 8;
      b.fill(fill, 3);
      relay.message(conns[i], b);
    };
    return { relay, conns, join, snaps, offer };
  }

  it("passes the host's progress to everyone else, not a guest's", () => {
    const { join, snaps, offer } = setup();
    const host = join("Host");
    const guest = join("Guest");
    offer(guest, 7);
    expect(snaps(host)).toEqual([]);
    offer(host, 9);
    expect(snaps(host)).toEqual([]);
    const got = snaps(guest);
    expect(got.length).toBe(1);
    expect(got[0][1]).toBe(1); // origin: the host's id
    expect(got[0].length).toBe(4 + 32);
    expect(got[0][4]).toBe(9);
  });

  it("sends the host's progress to a late joiner, and the next host's when the host leaves", () => {
    const { relay, conns, join, snaps, offer } = setup();
    const host = join("Host");
    offer(host, 9);
    const guest = join("Guest");
    expect(snaps(guest).map((m) => m[4])).toEqual([9]);
    const third = join("Third");
    offer(guest, 5);
    expect(snaps(third).map((m) => m[4])).toEqual([9]);
    relay.disconnect(conns[host]);
    // The guest is host now: its own progress goes to the third player.
    expect(snaps(third).map((m) => m[4])).toEqual([9, 5]);
    expect(snaps(guest).length).toBe(1);
  });

  it("drops a malformed or oversized snapshot", () => {
    const { join, snaps, offer } = setup();
    const host = join("Host");
    const guest = join("Guest");
    offer(host, 1, 0x1000);
    expect(snaps(guest)).toEqual([]);
  });
});

describe("player effects", () => {
  it("forwards a player's effects to the others, only under their own id", () => {
    const relay = new Relay({ fixedRooms: ["ROOM22"] });
    const inbox: Uint8Array[][] = [[], []];
    const conns: Conn[] = [0, 1].map((i) => ({ send: (b: Uint8Array) => inbox[i].push(b), close: () => {} }));
    conns.forEach((c, i) => {
      relay.connect(c);
      relay.message(c, hello(i ? "B" : "A", "ROOM22"));
    });
    const fx = (sender: number) => Uint8Array.from([Msg.PlayerEffect, sender, 1, 1, 7, 0, 0]);
    relay.message(conns[0], fx(1));
    relay.message(conns[0], fx(2)); // not A's id: dropped
    expect(inbox[1].filter((m) => m[0] === Msg.PlayerEffect).map((m) => m[1])).toEqual([1]);
    expect(inbox[0].filter((m) => m[0] === Msg.PlayerEffect)).toEqual([]);
  });

  it("forwards a player's sounds to the others, only under their own id", () => {
    const relay = new Relay({ fixedRooms: ["ROOM22"] });
    const inbox: Uint8Array[][] = [[], []];
    const conns: Conn[] = [0, 1].map((i) => ({ send: (b: Uint8Array) => inbox[i].push(b), close: () => {} }));
    conns.forEach((c, i) => {
      relay.connect(c);
      relay.message(c, hello(i ? "B" : "A", "ROOM22"));
    });
    const snd = (sender: number) => Uint8Array.from([Msg.PlayerSound, sender, 1, 1, 1, 0, 0, 0, 0, 0]);
    relay.message(conns[0], snd(1));
    relay.message(conns[0], snd(2)); // not A's id: dropped
    expect(inbox[1].filter((m) => m[0] === Msg.PlayerSound).map((m) => m[1])).toEqual([1]);
    expect(inbox[0].filter((m) => m[0] === Msg.PlayerSound)).toEqual([]);
  });
});
