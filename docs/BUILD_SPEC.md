# Build spec: Super Mario Galaxy co-op in the browser

Status: v1, 2026-10-12. Owner: Reece. Audience: the AI agents building
this, and the two humans driving them.

This is the full writeup. `skills/smg-coop/SKILL.md` is the short form an
agent loads every session. If the two disagree, this file wins, and the
skill gets fixed.

## 1. Goal

Super Mario Galaxy runs in a Chromium browser tab at 60 fps, 1280x720 or
better, from the user's own disc data, with up to 5 players online in the
same room. Each player controls their own Mario or Luigi. Players see each
other in the same galaxy. Stars and story progress are shared. Enemies and
objects are local to each player (Phase 1-3); a short list of key objects
is synced in Phase 4.

Personal research project. Not for distribution with game data.

## 2. Non-goals

- Split-screen or shared camera.
- Fully synced world (every enemy, every coin). sm64coopdx took years to do
  this for a far smaller game. Out of scope.
- Wii hardware target. We never build for the Wii.
- Mobile browsers before Phase 5.
- Contributing to Petari or AstroCore.

## 3. The hard constraint: game data

Petari is code. It has no assets. The game cannot start without a cooked
copy of a Super Mario Galaxy disc. Nothing in this repo, in CI, in any
build output or on any server this project runs may contain that data.

This has three consequences for the plan:

1. Each person who wants to run the game needs their own disc dump,
   extracted with Dolphin, cooked with `tools/cook/`. One-time, ~30 min.
2. CI can only prove the build boots against a synthetic stub game folder
   (right file names, empty or generated contents). Rendering correctness
   is checked by a human with real data.
3. The web shell must cook the user's disc in-browser and store the result
   in OPFS. The hosted page is code only. Anyone visiting it without their
   own disc sees an import screen and nothing else.

SMGwebport ships cooked game data in its repo. That is the thing that gets
a project taken down. We don't do that, and we don't link to it from
anything public.

## 4. Why this route

Four ways to get SMG into a browser were evaluated (see the research
report, 2026-10-12):

| Route | Verdict |
|---|---|
| Petari -> WASM with a GX translation layer | Chosen. GalaxyQuest proved the platform layer; SMGwebport proved it compiles with Emscripten. |
| Port AstroCore (Luma's Workshop PC port) | Not released. Human-only code policy. Watch, don't wait. |
| Dolphin compiled to WASM | Early demos only, software rasteriser, desktop Chrome only. Can't give 5 independent players. |
| noclip.website | Renderer only, no game logic. Use as a GX reference. |

Netcode: every working SMG and Sunshine online mod uses "independent worlds
+ puppet players + shared progress." sm64coopdx is the only one that syncs
the world, and it is a different scale of effort. We copy the proven model.

Transport: WebSocket relay first. At 5 players x 48 bytes x 30 Hz it is
~7 KB/s per client. TCP head-of-line blocking is a non-issue at this rate.
WebRTC and WebTransport add NAT traversal and browser-support pain for no
gain until we sync the world. Rollback (GGPO-style) needs cheap full-state
save/restore every frame; snapshotting an 88 MB Wii heap is not that.

## 5. Architecture

```
 browser tab
 +----------------------------------------------------------------+
 |  web/ (TypeScript, Vite)                                        |
 |   import screen -> cooker (WASM) -> OPFS                        |
 |   lobby, settings, player list, toasts, net status              |
 |   <canvas> (OffscreenCanvas transferred to the game worker)     |
 |   AudioWorklet (receives PCM from the game worker)              |
 |   Gamepad API + mouse -> SharedArrayBuffer input ring           |
 |   WebSocket client -> SharedArrayBuffer net ring                |
 +--------------------------------+-------------------------------+
                                  | postMessage + SAB
 +--------------------------------v-------------------------------+
 |  game worker (WASM, pthreads)                                   |
 |   decomp/   Petari game code, TARGET_WEB                        |
 |   platform/ gx->WebGL2, dsp->pcm ring, wpad<-input ring,        |
 |             dvd<-OPFS sync reads, nand<-IndexedDB, os           |
 |   net/      PlayerState out, remote states in, puppet spawner,  |
 |             progress event merge                                |
 +----------------------------------------------------------------+
                                  | wss
 +--------------------------------v-------------------------------+
 |  server/ (TypeScript, Node or Cloudflare Worker + Durable Obj)  |
 |   rooms, relay PlayerState, progress log, join-in-progress      |
 +----------------------------------------------------------------+
```

### 5.1 decomp/

Vendored from GalaxyQuest's `decomp/` at a pinned commit (record it in
`DECISIONS.md` D-001). This is Petari plus the `TARGET_PC` little-endian
and 64-bit fixes. We add `TARGET_WEB` where the web differs from the Quest
(it rarely should; the platform layer absorbs differences). Tag the vendored
base as `petari-base` so `git diff petari-base -- decomp` shows our changes.

Petari's own build (`configure.py`, `orig/`, `config/`) stays untouched and
unused. Our CMake picks up `decomp/src` and `decomp/include` directly.

### 5.2 platform/

One subdirectory per Wii subsystem, each with its own `CMakeLists.txt`.

- `gx/`: GX command stream -> WebGL2. Start from GalaxyQuest's GLES3
  renderer. Differences: no `glReadPixels` into a texture mid-frame (stub
  lens flare and any EFB copies that feed back into rendering; EFB copies
  to a texture used *later* are fine via framebuffer-to-texture), no
  `GL_TEXTURE_2D_ARRAY` tricks that need GLES3.1, compressed texture
  extension detection. Shader variant cache: serialise compiled TEV
  shader sources keyed by TEV config hash, ship a seed, warm on load.
- `audio/`: GalaxyQuest's DSP mixer. Output 48 kHz stereo float into a
  ring buffer in a SharedArrayBuffer; an AudioWorklet drains it. Resample
  if the AudioContext isn't 48 kHz.
- `input/`: WPAD surface. Reads a ring buffer the main thread fills from
  Gamepad API + mouse. Mapping in section 7.
- `fs/`: DVD reads become synchronous reads from OPFS via
  `FileSystemSyncAccessHandle` (worker-only API; this is one reason the
  game runs on a worker). NAND saves go to IndexedDB through an async
  bridge; the game's save path is already async-shaped (it polls).
- `os/`: threads (pthreads), timers (`emscripten_get_now`), the console
  memory layout GalaxyQuest sets up (MEM1/MEM2 arenas inside the WASM heap),
  and the main loop driven by `emscripten_set_main_loop` or a manual
  `requestAnimationFrame` proxy.
- `native/`: a desktop SDL2 + OpenGL backend so you can build and debug
  without a browser. Same `gx/` and `audio/` code, different window and
  input glue. This is where most iteration happens.

### 5.3 net/

C++ module linked into the game. Lives outside the scene heap so it
survives stage transitions.

- `NetClient`: owns the SAB ring to the main thread's WebSocket. Encodes
  local `PlayerState` every other frame (30 Hz). Decodes incoming states
  into per-player ring buffers.
- `RemotePlayerPuppet`: a `LiveActor` subclass with a Mario or Luigi model
  and an animation player. On scene enter, one is spawned per known remote
  player. Each frame it samples its ring at (now - 100 ms), interpolates,
  sets transform and animation. Skipped (not drawn, not updated) when the
  remote player's galaxy/scenario doesn't match.
- `ProgressSync`: receives reliable events, merges into local save data,
  raises a toast via the JS bridge. Sends local star-gets as events.
- JS bridge: `EM_JS` / `ccall` glue for toasts, player list, net status.

### 5.4 server/

TypeScript. Phase 2 runs on Node locally. Phase 3 moves to a Cloudflare
Worker with a Durable Object per room (free tier is enough for 5 players).
Protocol in `docs/NETCODE.md`.

- Rooms: 6-char code. Max 5 players. Host is whoever created it; host only
  matters for Phase 4 object authority.
- Relay: forwards `PlayerState` to all others in the room. No validation
  beyond size.
- Progress log: append-only per room. On join, send full log. Clients ack
  by sequence number; server resends unacked.
- No accounts. No persistence beyond room lifetime (24 h idle timeout).

### 5.5 web/

Vite + TypeScript. Minimal framework (Preact or plain). Screens:

1. Import: pick disc image or extracted folder. Run cooker. Progress bar.
   Request persistent storage. Store to OPFS.
2. Lobby: name, character (Mario/Luigi), colour tint for puppets, create
   or join room by code. Or "play solo".
3. Game: canvas, overlay with player list, net status, toasts, pause menu
   hook, settings (resolution scale, controls, volume).

The overlay is HTML on top of the canvas, not in-game UI. Cheaper and it's
where the design work goes.

### 5.6 tools/

- `cook/`: GalaxyQuest's `cook.py` (Python) for desktop use, and a port of
  it that runs in the browser. Phase 1 uses Pyodide to run the Python
  unchanged (slow but correct). Phase 5 rewrites the hot path in Rust/WASM
  if import time matters.
- `bench/`: `--headless` runner scripts, the synthetic stub game folder for
  CI, a frame-time capture script, and a baseline file CI compares to.

## 6. Phases

Each phase has an exit gate. Don't start the next phase's non-`parallel-ok`
tasks until the gate passes. Task IDs are in `TASKS.md`.

### Phase 0: Spike (2-4 weeks)

Goal: prove the toolchain end to end.

- Vendor decomp from GalaxyQuest. Build GalaxyQuest's native path with
  the OpenXR layer removed, as a flat desktop app (SDL2 window, keyboard).
- Get the native build to boot to the title screen with a cooked disc.
- Build the same with Emscripten. Boot to the title screen in Chromium
  with the cooked folder loaded from OPFS. Audio may be silent, input may
  be keyboard only.

Gate: `--headless --frames 600` exits 0 on native and web. Title screen
renders in a browser. One human has seen it.

### Phase 1: Single-player in the browser (4-8 weeks)

- In-browser cooker (Pyodide). Import screen. OPFS storage. Persistent
  storage request.
- Audio through AudioWorklet.
- Gamepad + mouse input with the mapping in section 7.
- Saves to IndexedDB.
- Shader cache seed.
- Frame pacing: 60 fps, resolution scale setting.
- Headless CI on the stub folder. Bench baseline captured.
- Play through the prologue and one full galaxy without a crash.

Gate: a human plays from new game to the first star of Good Egg Galaxy in
Chromium, with sound, on a gamepad, at 60 fps on the reference machine.

### Phase 2: Ghost co-op (3-6 weeks)

- `NetClient`, `PlayerState` encoding, SAB ring to a WebSocket.
- Relay server, local Node. Rooms by code.
- `RemotePlayerPuppet`: spawn, interpolate, animate, despawn.
- Same-galaxy visibility.
- Lobby screen. Player list overlay. Name tags above puppets (overlay,
  projected from world position via the JS bridge).

Gate: 3 people in a room see each other move in Good Egg Galaxy over the
internet, with no desync complaints at 100 ms interpolation delay.

### Phase 3: Shared progress (3-6 weeks)

- Progress event log on the server. Join-in-progress gets the full log.
- Star-get merge into local save. Toast on remote star-get.
- Story flag and galaxy unlock merge. Comet Observatory state.
- "Follow host" warp: a button that takes you to the host's galaxy and
  scenario.
- Cloudflare deploy of the relay.
- 5-player test.

Gate: 5 players, 2 hours, no one loses a star, late joiner sees the
right observatory state.

### Phase 4: Selective world sync (open-ended)

Host-authoritative sync for a short list, one object type per task:
switches, launch stars (already fired), coins (collected), 1-ups, bosses'
HP and phase. Each needs: object identity (galaxy + zone + object index),
an event schema, host logic, client apply logic, and a test. This phase
never finishes; it just gets better. Pick objects by what hurts most in
play testing.

### Phase 5: Polish (optional, open-ended)

- Rust/WASM cooker for fast import.
- Safari support (OffscreenCanvas + WebGL2 checks).
- Mobile touch controls.
- Aurora/WebGPU renderer spike, compared against WebGL2 on the bench.
- Local 2-player on one machine (two puppets? No: two real players. This
  is where single-player assumptions bite. Scope carefully.)

## 7. Input mapping

| Game | Keyboard | Gamepad (standard mapping) | Mouse |
|---|---|---|---|
| Move | WASD | Left stick | |
| Jump / A | Space | A (0) | |
| Spin | J or Shift | B (1) or X (2) | Left click |
| Shoot star bit / B | K | Right trigger (7) | Right click |
| Crouch / Z | Ctrl | Left trigger (6) | |
| Camera reset / C | C | Left bumper (4) | |
| Pointer | | Right stick moves a virtual cursor | Mouse position |
| D-pad camera | Arrows | D-pad (12-15) | |
| Pause | Esc | Start (9) | |
| Tilt (Star Ball, Ray) | A/D | Left stick | |

Pointer has to work from both stick and mouse because the pull-star
sections need it mid-platforming. Virtual cursor speed is a setting.

## 8. Reference machine and budgets

Reference: a 2022-era laptop with integrated graphics (Intel Iris Xe or
Apple M1), Chromium stable, 1280x720 canvas.

- 60 fps sustained in Good Egg, Honeyhive, and the Observatory with 4
  puppets visible. Frame time p95 under 16 ms.
- WASM heap under 384 MB.
- Import (cook) under 10 min for a 4.4 GB ISO without movies, Phase 1.
- Cold load to title screen under 15 s with a warm shader cache.
- Net: under 10 KB/s per client at 5 players.

Bench scripts in `tools/bench/` capture these. CI compares frame time
against a checked-in baseline on the stub folder (which measures the loop
and renderer setup, not real scenes).

## 9. Known problems and how to handle them

See `skills/smg-coop/SKILL.md` section 7 for the short list. Longer notes:

**Cross-origin isolation.** pthreads need SharedArrayBuffer, which needs
COOP/COEP response headers. Vite dev sets them. For a static host, ship
`coi-serviceworker.js` (MIT, from gzuidhof) which reloads the page with the
headers. Cloudflare Pages can set headers natively via `_headers`.

**Worker + OffscreenCanvas.** The game must not block the main thread, so
it runs on a pthread (`-sPROXY_TO_PTHREAD`) with the canvas transferred
(`-sOFFSCREENCANVAS_SUPPORT`). WebGL2 context is created on the worker. If
Emscripten's offscreen path fights you, fallback: run the game on the main
thread with `emscripten_set_main_loop` and accept jank during cooking.
Decide in Phase 0, record in `DECISIONS.md`.

**OPFS sync reads.** `FileSystemSyncAccessHandle` is worker-only and gives
real synchronous reads, which is what the DVD layer wants. Open handles are
exclusive per file; cache handles, don't reopen per read.

**Petari is "100% code" but not finished.** Maintainers say data linking is
still in progress. GalaxyQuest reports all 44 galaxies load, so the code is
good enough. Expect edge-case crashes in rarely visited places. Log, don't
block.

**Region.** GalaxyQuest tested EUR only. Petari targets KOR. If someone has
a US or JP disc and something breaks, it is probably a dol table offset in
the cooker. Add the region to `dol_tables.py` and move on.

**Puppet memory.** Each puppet loads a Mario or Luigi model into the scene
heap. The game already loads one. Four more may not fit in the Wii-sized
heap. We are not on a Wii; raise the arena size in `platform/os/` and
measure. If the game's own allocator fights, put puppet models in a
separate heap the game doesn't manage.

**Animation sync.** `PlayerState` carries an animation id and frame. The
animation set for Mario is large and some animations are blended. Phase 2
accepts visible snapping between animations. Phase 5 can add blend state.

## 10. Testing strategy

- Unit: doctest for `platform/` and `net/` (encoders, interpolators, ring
  buffers, OPFS stubs). vitest for `server/` and `web/`.
- Smoke: `--headless --frames 600` on the stub folder in CI. Exit 0 or the
  PR fails.
- Integration (human): a checklist in `docs/PLAYTEST.md` per phase gate.
  Someone with a cooked disc runs it and pastes results into the task log.
- Net soak: `server/test/soak.ts` spins 5 fake clients at 30 Hz for an
  hour against the relay and asserts no dropped sequence numbers.

## 11. Decision log seed

These are pre-made. Record them as D-001 to D-006 in `docs/DECISIONS.md`
when the repo is initialised, with the vendored commit hashes filled in.

- D-001: Vendor decomp from GalaxyQuest at commit `<hash>`, not from
  Petari directly. Reason: `TARGET_PC` fixes already done.
- D-002: WebGL2 for Phases 0-4. WebGPU/Aurora is a Phase 5 spike.
- D-003: WebSocket relay, snapshot interpolation, no rollback.
- D-004: Independent worlds. Puppets, not player actors, for remote
  players.
- D-005: Game runs on a pthread with OffscreenCanvas. Fallback is main
  thread. Decide by end of Phase 0.
- D-006: Pyodide for the in-browser cooker in Phase 1. Rewrite only if
  import time fails the budget.

## 12. References

- Petari: https://github.com/SMGCommunity/Petari (CC0)
- GalaxyQuest: https://github.com/bigmak94/GalaxyQuest (Unlicense). Read
  `docs/TECHNICAL.md` and `docs/CONTROLS.md`.
- SMGwebport: https://github.com/T4ctica1YT/SMGwebport (Emscripten flags
  only; do not use its data)
- smg-online: https://github.com/FimeV1/smg-online (design reference)
- sm64coopdx: https://github.com/coop-deluxe/sm64coopdx (sync model
  reference)
- noclip.website: https://github.com/magcius/noclip.website (GX reference)
- Aurora: https://github.com/encounter/aurora (MIT, Phase 5)
- coi-serviceworker: https://github.com/gzuidhof/coi-serviceworker
