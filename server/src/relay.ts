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
  encodeError,
  encodePlayerJoined,
  encodePlayerLeft,
  encodeWelcome,
  forwardPlayerState,
  type PlayerInfo,
} from "./protocol.ts";

export const MAX_PLAYERS = 5;
export const IDLE_DROP_MS = 15_000; // drop a client after 15 s of silence
export const RATE_LIMIT_PER_S = 60; // messages per client per second (spec)
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
}

interface Room {
  code: string;
  hostId: number;
  players: Map<number, Client>;
  emptySince: number | null;
}

export interface RelayOptions {
  now?: () => number;
  random?: () => number;
  log?: (msg: string) => void;
}

export class Relay {
  private readonly rooms = new Map<string, Room>();
  private readonly clients = new Map<Conn, Client>();
  private readonly now: () => number;
  private readonly random: () => number;
  private readonly log: (msg: string) => void;

  constructor(opts: RelayOptions = {}) {
    this.now = opts.now ?? Date.now;
    this.random = opts.random ?? Math.random;
    this.log = opts.log ?? (() => {});
  }

  connect(conn: Conn): void {
    const t = this.now();
    this.clients.set(conn, { conn, lastHeard: t, windowStart: t, windowCount: 0 });
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
      case Msg.Ping:
        // Echo the client's timestamp back.
        if (data.length >= 5) {
          const out = data.slice(0, 5);
          out[0] = Msg.Pong;
          conn.send(out);
        }
        break;
      default:
        break; // progress messages arrive with P3-01
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
    for (const [code, room] of this.rooms) {
      if (room.emptySince !== null && t - room.emptySince > ROOM_IDLE_MS) this.rooms.delete(code);
    }
  }

  roomCount(): number {
    return this.rooms.size;
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

  private leaveRoom(c: Client): void {
    const room = c.room;
    if (!room || !c.info) return;
    room.players.delete(c.info.id);
    c.room = undefined;
    const left = encodePlayerLeft(c.info.id);
    for (const other of room.players.values()) other.conn.send(left);
    if (room.players.size === 0) room.emptySince = this.now();
    else if (room.hostId === c.info.id) room.hostId = room.players.keys().next().value!;
    this.log(`${c.info.name} left ${room.code} (${room.players.size} players)`);
  }

  private createRoom(): Room {
    let code: string;
    do {
      code = "";
      for (let i = 0; i < 6; i++) code += CODE_ALPHABET[Math.floor(this.random() * CODE_ALPHABET.length)];
    } while (this.rooms.has(code));
    const room: Room = { code, hostId: 0, players: new Map(), emptySince: null };
    this.rooms.set(code, room);
    return room;
  }
}
