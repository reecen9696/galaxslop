# Netcode protocol v1

Scope: Phases 2-3. Phase 4 extends with object events.

## Model

Independent worlds. Every client runs the full game. The server relays
player state and keeps a progress log. It never simulates.

## Transport

WebSocket, binary frames. One connection per client. Heartbeat every 5 s.
Server drops a client after 15 s of silence. Clients reconnect with the
same `player_id` and get the progress log replayed.

## Framing

All messages: `u8 type` then payload. Little-endian. Floats are IEEE f32
unless marked f16.

| type | name | dir | reliable | payload |
|---|---|---|---|---|
| 0x01 | Hello | c->s | yes | u8 proto_ver, u8 name_len, name utf8, u8 character (0 Mario, 1 Luigi), u8 tint (see Client behaviour), u8 room_code_len, room_code ascii (0 len = create) |
| 0x02 | Welcome | s->c | yes | u8 player_id, u8 room_code_len, room_code, u8 player_count, then per player: u8 id, u8 name_len, name, u8 character, u8 tint |
| 0x03 | PlayerJoined | s->c | yes | u8 id, u8 name_len, name, u8 character, u8 tint |
| 0x04 | PlayerLeft | s->c | yes | u8 id |
| 0x13 | WorldEvent | c->s->others | no | u8 kind (1 enemy defeated, 2 coin/star bit collected, 3 coins dropped, 4 star bits dropped, 5 shooting star fires, 6 stage switch on/off (id = zone << 16 | switch, zone 0xFFFF global, a = on; area switches stay local), 7 event star appears (a boss beaten: the boss dies on the others and its star appears; name hash, id = star id, a = flags), 8 power star grabbed (id = star id: everyone in the level grabs it too, the star flies to them), 9 boss hit (a hit the local player landed on a boss, replayed on the others' boss: id = placement id, or for a boss part made at set-up the ordinal among actors of its name; pos = attacker offset from the sensor hit; a = message | sensor index << 16; b = attacker sensor type | placed << 16; the receiver delivers it from a puppet's sensor and retries for 3 s while its boss cannot take it)), u32 stage_hash, u8 scenario, u32 placement id (FNV-1a of the actor name and its placed position, net/world; 0 for a spawned actor) or drop count, u32 name hash, f32 pos[3], f32 a, f32 b (star bit launch speeds). Forwarded as is to everyone else in the room. Shooting stars are led by the stage host (lowest player id in the stage): its stars fire on their timer and say so, the others' fire when told. A collected purple coin (kind 2, a = 1) also counts on the others' purple coin counter (one shared count); star chips stay per player. Receivers play the enemy's Death effect and kill it (no loot); a spawned enemy is matched by name within 400 units. A player arriving in the stage is sent the visit's defeats again. |
| 0x14 | ActorState | c->s->others | no | u8 sender id (the relay checks it), u32 stage_hash, u8 scenario, u32 sender frame, u8 count, then count entries: u32 placement id, u32 nerve hash (FNV-1a of "Class::Nerve", 0 unknown), u16 nerve step, u8 flags (1 dead, 2 hidden, 4 collision valid, 8 claimed: the sender touches it), f32 pos[3], i16 quat[4] (/32767), u8 extras mask then per bit: 1 RailRider (f32 coord, f32 speed, u8 forward), 2 MapPartsRailMover (u32 nerve, u16 step, f32 speed, accel, _34, _48), 4 MapPartsRotator (u32 nerve, u16 step, f32 _18, target, speed, angle, _A0), 8 MapPartsRailRotator (u32 nerve, u16 step, f32 angle, target, speed), 16 animation (u8 len, BCK name, f32 frame, f32 rate). At most 1400 bytes, every 3 frames. Actor sync (net/world/actor_sync): each placed actor is run by one player, the nearest one in the stage (300 units of margin before it changes hands), or the one standing on it, holding it, hitting it or (NPCs) talking to or racing it; the owner sends what moved or changed, the others show their copy at the owner's extrapolated pose and animation and follow its nerve (map objects, rides, enemies; not NPCs, whose talks stay local) and its map parts' phase. Bosses, coins, star bits and the sky are left out. |
| 0x12 | Contact | c->s->target | no | u8 target_id (server -> target: sender's id), u8 kind (1 jumped on: flinch; 2 ground-pounded: squash, -1 health), f16 dir[3] (toucher's facing). Forwarded only to the target. D-020 |
| 0x10 | PlayerState | c->s->c | no | see below |
| 0x20 | ProgressEvent | c->s | yes | u16 kind, u32 stage_hash, u8 scenario, u16 arg |
| 0x21 | ProgressLog | s->c | yes | u32 from_seq, u16 count, then count x (u32 seq, u8 origin_id, u16 kind, u32 stage_hash, u8 scenario, u16 arg) |
| 0x22 | ProgressAck | c->s | yes | u32 seq |
| 0x30 | Ping | both | no | u32 t |
| 0x31 | Pong | both | no | u32 t |
| 0x40 | Error | s->c | yes | u8 code, u8 msg_len, msg |

### PlayerState (0x10)

Client sends at 30 Hz. Server prepends `u8 player_id` after the type byte
and forwards to all others in the room. Implemented in
`net/proto/player_state.{h,cpp}` (P2-01), tested in `net/tests`.

```
u8   type           0x10
u8   player_id      (server-added on forward)
u32  tick           client capture frame counter
f32  pos[3]
f16  rot[4]         orientation quaternion xyzw, from Mario's base matrix
f16  up[3]          gravity up vector
f16  vel[3]
u32  anim_hash      MR::getHashCode(current animation name); the receiver
                    calls XanimePlayer::changeAnimationByHash (D-018)
u8   anim_weight[4] track blend weights x255 (locomotion group, D-018)
f16  anim_frame
u16  flags          bit0 luigi, bit1 grounded, bit2 hidden, bit3 in_cutscene, bits4-5 eyelid (0 open..3 closed), bits6-9 power-up form (MarioActor PlayerMode), bit10 squashed
u32  stage_hash     MR::getHashCode(stage name): same galaxy check
u8   scenario
```
54 bytes from the client, 55 forwarded.

### ProgressEvent kinds

| kind | meaning | galaxy | scenario | arg |
|---|---|---|---|---|
| 1 | StarGet | galaxy | scenario | star index |
| 2 | StoryFlag | 0 | 0 | index into GameEventFlagTable |
| 3 | GalaxyUnlock | galaxy | 0 | 0 (unused) |
| 4 | DomeUnlock | 0 | 0 | dome id (unused) |
| 5 | GrandStar | galaxy | 0 | 0 (unused; still applied if received) |

StoryFlag is sent only for stored flags (mSaveFlag bit 0 clear) of the
types SpecialStar, EventFlag, StoryEvent and Galaxy. Computed flags such as
grand stars, galaxy and dome unlocks follow from the shared stars and send
nothing, so kinds 3-5 go unused (P3-03).

Phase 4 adds 100+ for objects.

## Client behaviour

- Send `PlayerState` every 2nd frame at 60 fps. Skip if nothing changed
  for 1 s (sleeping), then resume on any change.
- Keep a ring of the last 32 states per remote player.
- Render puppets at `now - 100 ms`. Find the two samples bracketing that
  time; Hermite interpolate pos with vel as tangents; slerp rot; lerp up;
  snap anim when the hash changes, else lerp frame.
- No bracketing sample for 500 ms: freeze. 3 s: hide. New sample: show.
- Draw puppet only if `galaxy_id` and `scenario_id` match local.
- `in_cutscene` flag: hide puppet (they're in a demo we aren't in).
- Spawn spacing: on each scene's first grounded, cutscene-free frame, if
  anyone else is in the room, the local Mario steps aside by a slot fixed
  by player id (1 stays, 2 right, 3 left, 4 further right, 5 further left;
  100 units each), only onto floor and not through a wall.
  `SMG_SPAWN_SPACING=0` turns it off.
- Puppets use the player models (Mario.arc / Luigi.arc). The character
  comes from the `PlayerState` Luigi flag (what the save plays), falling
  back to Hello's `character` before any state arrives.
- `tint` recolours a player's shirt and cap (`net/puppet/puppet_tint.h`),
  on every client including their own: 0 auto, 1 classic, 2 green (red on
  Luigi), 3 pink, 4 orange, 5 purple, 6 cyan, 7 yellow, 8 white, 9 black.
  Auto is by player id: 1 classic, 2 green, 3 pink, 4 orange, 5 purple.
  Clients send `SMG_TINT`; the web lobby sets it from its colour swatches.

## Progress merge

- Local save data is the source of truth for what the local player did.
- On `ProgressLog`, for each event: if the local save lacks it, apply it
  (set the star bit, set the flag) and raise a toast naming `origin_id`.
  Never clear anything.
- Ack the highest contiguous `seq` applied. Server resends from the next
  unacked seq every 2 s.
- Local star-get: send `ProgressEvent` immediately and keep sending until
  the server's next `ProgressLog` includes it. The server is idempotent on
  `(kind, stage_hash, scenario, arg)`, i.e. on what happened, not who did
  it: a star is a fact about the shared save, so a second player getting
  it adds no entry and no second toast. A duplicate is answered with the
  log from the sender's acked position, so it learns the event is in.
- Events and the log are broadcast to everyone in the room, the sender
  included (that is its confirmation). Implemented in `server/src/relay.ts`.
- Join-in-progress: `Welcome` is followed by `ProgressLog` from seq 0.

## Server behaviour

- Room: code, host_id, players (max 5), progress log (array), created_at.
- Relay `PlayerState` without inspection beyond length check.
- Append `ProgressEvent` to the log with a new `seq` if not a duplicate.
  Broadcast as a 1-item `ProgressLog`.
- Idle room (no connections 24 h): delete.
- Rate limit: 120 messages/s per client (PlayerState 30 + ActorState 20 + events), drop beyond.

## What is not in v1

- Encryption beyond TLS. No auth. Room code is the secret.
- Spectators.
- Voice.
- Any object or enemy state. See Phase 4.
- Clock sync. Interpolation uses receive time, not sender tick, for v1.
  Tick is carried for Phase 4.
