// Rooms and relaying, independent of the transport (main.ts wires it to
// WebSockets; tests can drive it directly).  docs/NETCODE.md, "Server
// behaviour".  No game logic: the relay never simulates anything.
import {
  ErrorCode,
  Msg,
  PLAYER_STATE_MAX,
  PLAYER_STATE_MIN,
  PROTO_VERSION,
  decodeHello,
  decodeProgressEvent,
  decodeSnapshot,
  encodeSnapshot,
  encodeError,
  encodeProgressLog,
  MAX_LOG_PER_MESSAGE,
  type LoggedEvent,
  encodePlayerJoined,
  encodePlayerLeft,
  encodeWelcome,
  forwardPlayerState,
  forwardContact,
  CONTACT_SIZE,
  WORLD_EVENT_SIZE,
  ACTOR_STATE_MIN,
  ACTOR_STATE_MAX,
  type PlayerInfo,
} from "./protocol.ts";

export const MAX_PLAYERS = 5;
export const IDLE_DROP_MS = 15_000; // drop a client after 15 s of silence
export const RATE_LIMIT_PER_S = 160; // messages per client per second: PlayerState 30 + ActorState 20 + PlayerSound 30 + effects and events
export const RESEND_MS = 2000; // unacknowledged progress is resent this often
const ROOM_IDLE_MS = 24 * 60 * 60 * 1000;
// No 0/O, 1/I/L: room codes are read out loud.
const CODE_ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";

export interface Conn {
  send(b: Uint8Array): void;
  close(): void;
}

interface Client {
  conn: Conn;
  room?: Room;
  info?: PlayerInfo;
  lastHeard: number;
  windowStart: number;
  windowCount: number;
  acked: number; // highest contiguous progress seq the client confirmed (-1: none)
  lastLogSent: number;
  snapshot?: Uint8Array; // this client's progress (P3-07); only the host's is passed on
}

interface Room {
  code: string;
  fixed: boolean;
  hostId: number;
  players: Map<number, Client>;
  emptySince: number | null;
  log: LoggedEvent[]; // append-only; seq = index
  logKeys: Set<string>; // what has happened, whoever did it first
}

export interface RelayOptions {
  // Permanent rooms that exist from the start and are never deleted (local
  // play and tests: everyone joins by a known code).
  fixedRooms?: string[];
  now?: () => number;
  random?: () => number;
  log?: (msg: string) => void;
  // A player left a room (the leaderboard forgets their scores).
  onLeave?: (code: string, id: number) => void;
}

export class Relay {
  private readonly rooms = new Map<string, Room>();
  private readonly clients = new Map<Conn, Client>();
  private readonly now: () => number;
  private readonly random: () => number;
  private readonly log: (msg: string) => void;
  private readonly onLeave: (code: string, id: number) => void;

  constructor(opts: RelayOptions = {}) {
    this.now = opts.now ?? Date.now;
    this.random = opts.random ?? Math.random;
    this.log = opts.log ?? (() => {});
    this.onLeave = opts.onLeave ?? (() => {});
    for (const code of opts.fixedRooms ?? []) {
      const c = code.trim().toUpperCase();
      if (c) this.rooms.set(c, { code: c, fixed: true, hostId: 0, players: new Map(), emptySince: null, log: [], logKeys: new Set() });
    }
  }

  connect(conn: Conn): void {
    const t = this.now();
    this.clients.set(conn, { conn, lastHeard: t, windowStart: t, windowCount: 0, acked: -1, lastLogSent: 0 });
  }

  disconnect(conn: Conn): void {
    const c = this.clients.get(conn);
    if (!c) return;
    this.clients.delete(conn);
    this.leaveRoom(c);
  }

  message(conn: Conn, data: Uint8Array): void {
    const c = this.clients.get(conn);
    if (!c || data.length === 0) return;
    const t = this.now();
    c.lastHeard = t;
    if (t - c.windowStart >= 1000) {
      c.windowStart = t;
      c.windowCount = 0;
    }
    if (++c.windowCount > RATE_LIMIT_PER_S) return; // drop beyond the limit

    switch (data[0]) {
      case Msg.Hello:
        this.onHello(c, data);
        break;
      case Msg.PlayerState:
        this.onPlayerState(c, data);
        break;
      case Msg.Contact:
        this.onContact(c, data);
        break;
      case Msg.WorldEvent:
        if (c.room && c.info && data.length === WORLD_EVENT_SIZE) {
          for (const other of c.room.players.values()) if (other !== c) other.conn.send(data);
        }
        break;
      case Msg.ActorState:
        // The sender byte must be the sender: no speaking for someone else.
        if (c.room && c.info && data.length >= ACTOR_STATE_MIN && data.length <= ACTOR_STATE_MAX && data[1] === c.info.id) {
          for (const other of c.room.players.values()) if (other !== c) other.conn.send(data);
        }
        break;
      case Msg.PlayerEffect:
      case Msg.PlayerSound:
        // The sender's particle effects and sounds: forwarded like ActorState.
        if (c.room && c.info && data.length >= 3 && data.length <= ACTOR_STATE_MAX && data[1] === c.info.id) {
          for (const other of c.room.players.values()) if (other !== c) other.conn.send(data);
        }
        break;
      case Msg.Ping:
        // Echo the client's timestamp back.
        if (data.length >= 5) {
          const out = data.slice(0, 5);
          out[0] = Msg.Pong;
          conn.send(out);
        }
        break;
      case Msg.ProgressEvent:
        this.onProgressEvent(c, data);
        break;
      case Msg.ProgressSnapshot:
        this.onSnapshot(c, data);
        break;
      case Msg.ProgressAck:
        if (data.length >= 5) {
          const seq = new DataView(data.buffer, data.byteOffset, data.byteLength).getUint32(1, true);
          if (c.room && seq < c.room.log.length && seq > c.acked) c.acked = seq;
        }
        break;
      default:
        break;
    }
  }

  // Drops silent clients and long-empty rooms; call every few seconds.
  tick(): void {
    const t = this.now();
    for (const c of [...this.clients.values()]) {
      if (t - c.lastHeard > IDLE_DROP_MS) {
        this.log(`drop ${c.info?.name ?? "?"}: silent for ${IDLE_DROP_MS} ms`);
        c.conn.close();
        this.disconnect(c.conn);
      }
    }
    // Resend unacknowledged progress (NETCODE "Progress merge").
    for (const c of this.clients.values()) {
      if (c.room && c.acked + 1 < c.room.log.length && t - c.lastLogSent >= RESEND_MS) this.sendLog(c, c.acked + 1);
    }
    for (const [code, room] of this.rooms) {
      if (!room.fixed && room.emptySince !== null && t - room.emptySince > ROOM_IDLE_MS) this.rooms.delete(code);
    }
  }

  roomCount(): number {
    return this.rooms.size;
  }

  // For the lobby's check before joining (GET /room/<code>): null when there
  // is no such room.
  roomInfo(code: string): { players: number; max: number } | null {
    const room = this.rooms.get(code.toUpperCase());
    return room ? { players: room.players.size, max: MAX_PLAYERS } : null;
  }

  // Who is in a room now (the leaderboard lists only them).
  members(code: string): PlayerInfo[] | null {
    const room = this.rooms.get(code.toUpperCase());
    return room ? [...room.players.values()].map((p) => p.info!) : null;
  }

  private onHello(c: Client, data: Uint8Array): void {
    if (c.room) return; // one Hello per connection
    let hello;
    try {
      hello = decodeHello(data);
    } catch {
      c.conn.send(encodeError(ErrorCode.BadMessage, "bad Hello"));
      return;
    }
    if (hello.protoVer !== PROTO_VERSION) {
      c.conn.send(encodeError(ErrorCode.ProtocolVersion, `protocol ${hello.protoVer}, server speaks ${PROTO_VERSION}`));
      return;
    }
    let room: Room | undefined;
    if (hello.roomCode === "") {
      room = this.createRoom();
    } else {
      room = this.rooms.get(hello.roomCode.toUpperCase());
      if (!room) {
        c.conn.send(encodeError(ErrorCode.RoomNotFound, `no room ${hello.roomCode}`));
        return;
      }
    }
    if (room.players.size >= MAX_PLAYERS) {
      c.conn.send(encodeError(ErrorCode.RoomFull, `room ${room.code} is full`));
      return;
    }
    let id = 1;
    while (room.players.has(id)) id++;
    const info: PlayerInfo = { id, name: hello.name.slice(0, 32), character: hello.character & 1, tint: hello.tint };
    c.room = room;
    c.info = info;
    if (room.players.size === 0) room.hostId = id;
    room.players.set(id, c);
    room.emptySince = null;

    const everyone = [...room.players.values()].map((p) => p.info!);
    c.conn.send(encodeWelcome(id, room.code, everyone));
    c.acked = -1;
    if (room.log.length > 0) this.sendLog(c, 0); // join-in-progress: the whole log
    const host = room.players.get(room.hostId);
    if (host && host !== c && host.snapshot) c.conn.send(encodeSnapshot(room.hostId, host.snapshot));
    const joined = encodePlayerJoined(info);
    for (const other of room.players.values()) if (other !== c) other.conn.send(joined);
    this.log(`${info.name} joined ${room.code} as ${id} (${room.players.size} players)`);
  }

  private onPlayerState(c: Client, data: Uint8Array): void {
    const room = c.room;
    if (!room || !c.info) return;
    if (data.length < PLAYER_STATE_MIN || data.length > PLAYER_STATE_MAX) return;
    const out = forwardPlayerState(c.info.id, data);
    for (const other of room.players.values()) if (other !== c) other.conn.send(out);
  }

  private onContact(c: Client, data: Uint8Array): void {
    const room = c.room;
    if (!room || !c.info || data.length !== CONTACT_SIZE) return;
    const target = room.players.get(data[1]);
    if (!target || target === c) return;
    target.conn.send(forwardContact(c.info.id, data));
  }

  private onProgressEvent(c: Client, data: Uint8Array): void {
    const room = c.room;
    if (!room || !c.info) return;
    const e = decodeProgressEvent(data);
    if (!e) return;
    // Idempotent on what happened, not who did it: a star is a fact about the
    // shared save, so a second player getting it adds nothing.
    const key = `${e.kind}:${e.stageHash}:${e.scenario}:${e.arg}`;
    if (room.logKeys.has(key)) {
      this.sendLog(c, Math.max(0, c.acked + 1)); // the sender learns it is in
      return;
    }
    room.logKeys.add(key);
    const entry: LoggedEvent = { ...e, seq: room.log.length, origin: c.info.id };
    room.log.push(entry);
    const msg = encodeProgressLog(entry.seq, [entry]);
    for (const p of room.players.values()) {
      p.conn.send(msg);
      p.lastLogSent = this.now();
    }
    this.log(`progress ${room.code}#${entry.seq} from ${c.info.name}: kind ${e.kind} stage ${e.stageHash.toString(16)}/${e.scenario} arg ${e.arg}`);
  }

  // The room takes its host's progress (P3-07): every client sends its own,
  // the host's goes to everyone else, now and to each joiner (onHello), and
  // the next host's when the host leaves (leaveRoom).
  private onSnapshot(c: Client, data: Uint8Array): void {
    const room = c.room;
    if (!room || !c.info) return;
    const snap = decodeSnapshot(data);
    if (!snap) return;
    c.snapshot = snap;
    if (room.hostId === c.info.id) this.sendHostSnapshot(room);
  }

  private sendHostSnapshot(room: Room): void {
    const host = room.players.get(room.hostId);
    if (!host?.snapshot) return;
    const msg = encodeSnapshot(room.hostId, host.snapshot);
    for (const p of room.players.values()) if (p !== host) p.conn.send(msg);
  }

  private sendLog(c: Client, from: number): void {
    const room = c.room;
    if (!room) return;
    const events = room.log.slice(from, from + MAX_LOG_PER_MESSAGE);
    if (events.length === 0) return;
    c.conn.send(encodeProgressLog(from, events));
    c.lastLogSent = this.now();
  }

  roomLog(code: string): LoggedEvent[] {
    return this.rooms.get(code)?.log ?? [];
  }

  private leaveRoom(c: Client): void {
    const room = c.room;
    if (!room || !c.info) return;
    room.players.delete(c.info.id);
    c.room = undefined;
    this.onLeave(room.code, c.info.id);
    const left = encodePlayerLeft(c.info.id);
    for (const other of room.players.values()) other.conn.send(left);
    if (room.players.size === 0) room.emptySince = this.now();
    else if (room.hostId === c.info.id) {
      room.hostId = room.players.keys().next().value!;
      this.sendHostSnapshot(room);
    }
    this.log(`${c.info.name} left ${room.code} (${room.players.size} players)`);
  }

  private createRoom(): Room {
    let code: string;
    do {
      code = "";
      for (let i = 0; i < 6; i++) code += CODE_ALPHABET[Math.floor(this.random() * CODE_ALPHABET.length)];
    } while (this.rooms.has(code));
    const room: Room = { code, fixed: false, hostId: 0, players: new Map(), emptySince: null, log: [], logKeys: new Set() };
    this.rooms.set(code, room);
    return room;
  }
}
