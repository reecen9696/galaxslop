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
  Contact: 0x12,
  WorldEvent: 0x13,
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

// Shared progress (P3-01).  ProgressEvent c->s: u16 kind, u32 stage_hash,
// u8 scenario, u16 arg.  ProgressLog s->c: u32 from_seq, u16 count, then
// count x (u32 seq, u8 origin_id, u16 kind, u32 stage_hash, u8 scenario,
// u16 arg).  ProgressAck c->s: u32 seq.
export interface ProgressEvent {
  kind: number;
  stageHash: number;
  scenario: number;
  arg: number;
}

export interface LoggedEvent extends ProgressEvent {
  seq: number;
  origin: number;
}

export const PROGRESS_EVENT_SIZE = 1 + 2 + 4 + 1 + 2;
export const MAX_LOG_PER_MESSAGE = 200;

export function encodeProgressEvent(e: ProgressEvent): Uint8Array {
  const b = new Uint8Array(PROGRESS_EVENT_SIZE);
  const v = new DataView(b.buffer);
  b[0] = Msg.ProgressEvent;
  v.setUint16(1, e.kind, true);
  v.setUint32(3, e.stageHash >>> 0, true);
  b[7] = e.scenario;
  v.setUint16(8, e.arg, true);
  return b;
}

export function decodeProgressEvent(b: Uint8Array): ProgressEvent | null {
  if (b.length < PROGRESS_EVENT_SIZE || b[0] !== Msg.ProgressEvent) return null;
  const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
  return { kind: v.getUint16(1, true), stageHash: v.getUint32(3, true), scenario: b[7], arg: v.getUint16(8, true) };
}

const LOG_ENTRY_SIZE = 4 + 1 + 2 + 4 + 1 + 2;

export function encodeProgressLog(fromSeq: number, events: LoggedEvent[]): Uint8Array {
  const b = new Uint8Array(1 + 4 + 2 + events.length * LOG_ENTRY_SIZE);
  const v = new DataView(b.buffer);
  b[0] = Msg.ProgressLog;
  v.setUint32(1, fromSeq, true);
  v.setUint16(5, events.length, true);
  let o = 7;
  for (const e of events) {
    v.setUint32(o, e.seq, true);
    b[o + 4] = e.origin;
    v.setUint16(o + 5, e.kind, true);
    v.setUint32(o + 7, e.stageHash >>> 0, true);
    b[o + 11] = e.scenario;
    v.setUint16(o + 12, e.arg, true);
    o += LOG_ENTRY_SIZE;
  }
  return b;
}

export function decodeProgressLog(b: Uint8Array): { fromSeq: number; events: LoggedEvent[] } {
  const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
  if (b[0] !== Msg.ProgressLog) throw new Error("not ProgressLog");
  const fromSeq = v.getUint32(1, true);
  const n = v.getUint16(5, true);
  const events: LoggedEvent[] = [];
  let o = 7;
  for (let i = 0; i < n; i++) {
    events.push({
      seq: v.getUint32(o, true),
      origin: b[o + 4],
      kind: v.getUint16(o + 5, true),
      stageHash: v.getUint32(o + 7, true),
      scenario: b[o + 11],
      arg: v.getUint16(o + 12, true),
    });
    o += LOG_ENTRY_SIZE;
  }
  return { fromSeq, events };
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

// Contact (D-020): client -> server [0x12][u8 target_id][u8 kind][f16 dir x3];
// server -> target only, the same bytes with the target id replaced by the
// sender's.  Unreliable, not inspected beyond length and target.
export const CONTACT_SIZE = 9;
// WorldEvent (world sync, net/world): [0x13][u8 kind][u32 stage][u8 scenario]
// [u32 placement id or count][u32 name hash][f32 pos x3][f32 a][f32 b],
// forwarded as is to everyone else in the room.  Unreliable.
export const WORLD_EVENT_SIZE = 35;
export function forwardContact(fromId: number, b: Uint8Array): Uint8Array {
  const out = b.slice(0, CONTACT_SIZE);
  out[1] = fromId;
  return out;
}

// Server -> others: [0x10][player_id][the sender's payload after its type byte].
export function forwardPlayerState(fromId: number, b: Uint8Array): Uint8Array {
  const out = new Uint8Array(b.length + 1);
  out[0] = Msg.PlayerState;
  out[1] = fromId;
  out.set(b.subarray(1), 2);
  return out;
}
