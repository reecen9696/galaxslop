// Wire format, docs/NETCODE.md.  Every message is `u8 type` then a payload,
// little-endian.  Only the messages the relay reads or writes are decoded
// here; PlayerState is forwarded without inspection beyond its length.

export const PROTO_VERSION = 1;

export const Msg = {
  Hello: 0x01,
  Welcome: 0x02,
  PlayerJoined: 0x03,
  PlayerLeft: 0x04,
  PlayerState: 0x10,
  ProgressEvent: 0x20,
  ProgressLog: 0x21,
  ProgressAck: 0x22,
  Ping: 0x30,
  Pong: 0x31,
  Error: 0x40,
} as const;

export const ErrorCode = {
  BadMessage: 1,
  ProtocolVersion: 2,
  RoomNotFound: 3,
  RoomFull: 4,
  RateLimited: 5,
} as const;

// PlayerState as the client sends it (without the server-added player id):
// 58 bytes forwarded minus the id byte and the type byte.  The relay only
// bounds-checks it; the layout is the clients' business.
export const PLAYER_STATE_MIN = 1 + 40;
export const PLAYER_STATE_MAX = 1 + 128;

export interface PlayerInfo {
  id: number;
  name: string;
  character: number; // 0 Mario, 1 Luigi
  tint: number;
}

export interface Hello {
  protoVer: number;
  name: string;
  character: number;
  tint: number;
  roomCode: string; // "" = create a room
}

const enc = new TextEncoder();
const dec = new TextDecoder("utf-8", { fatal: false });

class Reader {
  private off = 0;
  private readonly b: Uint8Array;
  constructor(b: Uint8Array) {
    this.b = b;
  }
  u8(): number {
    if (this.off >= this.b.length) throw new Error("short message");
    return this.b[this.off++];
  }
  str(): string {
    const n = this.u8();
    if (this.off + n > this.b.length) throw new Error("short string");
    const s = dec.decode(this.b.subarray(this.off, this.off + n));
    this.off += n;
    return s;
  }
}

export function decodeHello(b: Uint8Array): Hello {
  const r = new Reader(b);
  if (r.u8() !== Msg.Hello) throw new Error("not Hello");
  const protoVer = r.u8();
  const name = r.str();
  const character = r.u8();
  const tint = r.u8();
  const roomCode = r.str();
  return { protoVer, name, character, tint, roomCode };
}

class Writer {
  private parts: number[] = [];
  u8(v: number): this {
    this.parts.push(v & 0xff);
    return this;
  }
  str(s: string): this {
    const bytes = enc.encode(s).subarray(0, 255);
    this.u8(bytes.length);
    for (const x of bytes) this.parts.push(x);
    return this;
  }
  bytes(): Uint8Array {
    return Uint8Array.from(this.parts);
  }
}

export function encodeHello(h: Hello): Uint8Array {
  return new Writer().u8(Msg.Hello).u8(h.protoVer).str(h.name).u8(h.character).u8(h.tint).str(h.roomCode).bytes();
}

function writePlayer(w: Writer, p: PlayerInfo): void {
  w.u8(p.id).str(p.name).u8(p.character).u8(p.tint);
}

export function encodeWelcome(you: number, roomCode: string, players: PlayerInfo[]): Uint8Array {
  const w = new Writer().u8(Msg.Welcome).u8(you).str(roomCode).u8(players.length);
  for (const p of players) writePlayer(w, p);
  return w.bytes();
}

export function decodeWelcome(b: Uint8Array): { you: number; roomCode: string; players: PlayerInfo[] } {
  const r = new Reader(b);
  if (r.u8() !== Msg.Welcome) throw new Error("not Welcome");
  const you = r.u8();
  const roomCode = r.str();
  const n = r.u8();
  const players: PlayerInfo[] = [];
  for (let i = 0; i < n; i++) players.push({ id: r.u8(), name: r.str(), character: r.u8(), tint: r.u8() });
  return { you, roomCode, players };
}

export function encodePlayerJoined(p: PlayerInfo): Uint8Array {
  const w = new Writer().u8(Msg.PlayerJoined);
  writePlayer(w, p);
  return w.bytes();
}

export function encodePlayerLeft(id: number): Uint8Array {
  return Uint8Array.of(Msg.PlayerLeft, id);
}

export function encodeError(code: number, msg: string): Uint8Array {
  return new Writer().u8(Msg.Error).u8(code).str(msg).bytes();
}

// Server -> others: [0x10][player_id][the sender's payload after its type byte].
export function forwardPlayerState(fromId: number, b: Uint8Array): Uint8Array {
  const out = new Uint8Array(b.length + 1);
  out[0] = Msg.PlayerState;
  out[1] = fromId;
  out.set(b.subarray(1), 2);
  return out;
}
