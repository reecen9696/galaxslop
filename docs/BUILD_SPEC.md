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
| Petari -> WASM with a GX layer | Chosen. This is how every browser multiplayer port of a Nintendo game is built (sm64js, sm64coopdx web ports, sm64-coop-web): decomp + Emscripten + user-supplied ROM + a relay. GalaxyQuest proved the Wii replacement layer on Petari; SMGwebport (closed) proved it runs under Emscripten. GX layer: Aurora on WebGPU first (D-014; used by AstroCore, Metaforce, WiiCompiled; shown in-browser by `webcompiled`), GalaxyQuest's renderer reworked for WebGL2 as fallback (D-008). |
| Static recompilation (WiiCompiled) | Generic translator, per-game runtime; recompiles on the user's machine (30 min, 20 GB); opaque to modding. Petari is 100% source, strictly better. Its Aurora integration is the precedent we borrow. (D-015) |
| Dolphin compiled to WASM (`wasm-dolphin`) | Serious prototype, but Wii runs software-rasterised with no Wii Remote; Melee at best ~68-100% speed. Not 60 fps SMG. (D-015) |
| Port AstroCore (Luma's Workshop PC port) | Not released. Human-only code policy. Watch, don't wait. |
| Cloud-stream Dolphin + smg-online to browsers | Works today, no port work, GPU-box cost and 30-80 ms. Not the project; the right playtest baseline (P0-00). |
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
 |   <canvas> (WebGPU; OffscreenCanvas on the game worker, D-005)  |
 |   AudioWorklet (receives PCM from the game worker)              |
 |   Gamepad API + mouse -> SharedArrayBuffer input ring           |
 |   WebSocket client -> SharedArrayBuffer net ring                |
 +--------------------------------+-------------------------------+
                                  | postMessage + SAB
 +--------------------------------v-------------------------------+
 |  game worker (WASM, pthreads)                                   |
 |   decomp/   Petari game code, TARGET_WEB                        |
 |   platform/ gx->Aurora->WebGPU, dsp->pcm ring, wpad<-input ring, |
 |             dvd<-OPFS sync reads, nand<-IndexedDB, os           |
 |   net/      PlayerState out, remote states in, puppet spawner,  |
 |             progress event merge                                |
 +----------------------------------------------------------------+
                                  | wss
 +--------------------------------v-------------------------------+
 |  server/ (TypeScript, Node + ws, deployed as a container)       |
 |   rooms, relay PlayerState, progress log, join-in-progress      |
 +----------------------------------------------------------------+
```

### 5.1 decomp/

Vendored from GalaxyQuest at commit `d8a039c` together with `platform/` and
`tools/cook/` (D-007; the three were built against each other and are taken
as one snapshot). This is Petari plus the `TARGET_PC` little-endian and
64-bit fixes and some VR hooks. We add `TARGET_WEB` where the web differs
from the Quest (it rarely should; the platform layer absorbs differences).
The `petari-base` tag marks the Petari commit GalaxyQuest started from, so
`git diff petari-base -- decomp` shows all port changes, theirs and ours.

Current state (2026-10-12, P0-02 done): `petari-base` tags Petari
`0a5c660`; `decomp/` holds GalaxyQuest's port on top of it. Petari's full
upstream history is still in this repo, so a later rebase onto upstream is
a merge, not a re-vendor.

Petari's matching-build tooling (`configure.py`, `orig/`, `config/`) is not
in `decomp/`; GalaxyQuest dropped it and we don't build for the Wii. Our
CMake picks up `decomp/src`, `decomp/include` and `decomp/libs` directly.
GalaxyQuest's CMake is the starting point: it globs `Game`, `JSystem`,
`nw4r` and six RVL_SDK modules (`gx gd mem arc tpl aralt`, minus `GXFifo.c`
and `GXPerf.c`), compiles as C++20/C11 with `-fshort-wchar -fsigned-char
-fno-strict-aliasing -fwrapv -fms-extensions`, no exceptions, no RTTI, and
force-includes `platform/include/port/compat.h`. Keep those flags.

### 5.2 platform/

GalaxyQuest's layout, kept as is so its fixes can be pulled later:
`platform/src/<subsystem>/` with `platform/include/`. Subsystems: `os`,
`dvd`, `nand`, `input`, `sdk` (THP decoder and other SDK bits), `gx`, `vi`
(video interface, retrace), `audio`, `port`, plus ours: `native` (desktop host) and `web` (Emscripten
glue). `xr` is deleted. One `CMakeLists.txt` per subsystem. The GX layer
itself is not in `platform/`; it is Aurora, a pinned submodule in
`extern/aurora`.

- `gx/`: the game's GX calls go to Aurora (D-014). Aurora implements the
  GX API behind SDK-compatible headers (`include/dolphin/gx.h`), decodes
  hardware FIFO opcodes and baked display lists in its command processor,
  generates WGSL for TEV, and renders through WebGPU: emdawnwebgpu in the
  browser, Dawn (Metal/Vulkan/D3D12) natively. It has a per-array
  little-endian flag, so cooked vertex data is a supported input, and a
  pipeline cache with transferable entries that we persist to OPFS. Our
  `gx/` holds only the glue: FIFO hand-off, the EFB-copy and depth-snapshot
  requests SMG makes, `VI` timing, and the null backend for headless CI.
  Petari's `src/RVL_SDK/gx` is not compiled. GalaxyQuest's GLES 3.2
  renderer (FIFO recorder, display-list cache, batching, GLSL shader gen)
  stays in the tree as the WebGL2 fallback (D-008, P0-10) and as a
  reference for what SMG actually asks of GX. Already unsupported in both
  and staying so: `GXPeekARGB`/`GXPeekZ` readback (the sun's lens flare).
- `audio/`: GalaxyQuest's DSP mixer. Output 48 kHz stereo float into a
  ring buffer in a SharedArrayBuffer; an AudioWorklet drains it. Resample
  if the AudioContext isn't 48 kHz.
- `input/`: WPAD surface. Reads a ring buffer the main thread fills from
  Gamepad API + mouse. Mapping in section 7.
- `dvd/` and `nand/`: DVD reads become synchronous reads from OPFS via
  `FileSystemSyncAccessHandle` (worker-only API; this is one reason the
  game runs on a worker). GalaxyQuest's `dvd/` already handles region
  detection from the FST and loads the DOL tables (`GalaxyID.bcsv`,
  `StoryEvent.bcsv`, `ErrorMessageArchive.arc`) from the cooked `sys/`
  folder; keep that. NAND saves go to IndexedDB through an async bridge;
  the game's save path is already async-shaped (it polls).
- `os/`: GalaxyQuest emulates OS threads, alarms and interrupts in its own
  scheduler; the game is effectively single-threaded. Real threads are the
  frame builder and audio. Timers via `emscripten_get_now`. Memory layout
  per D-013: MEM1 at 0x80000000, MEM2 at 0x90000000 inside the wasm heap
  to start with. Main loop driven by `emscripten_set_main_loop` or a
  manual `requestAnimationFrame` proxy.
- `native/`: the desktop host, built on Aurora's `aurora::main` (SDL3
  window, gamepad, Dawn device) so you can build and debug without a
  browser. Same `gx/` and `audio/` code as the web build, different host
  glue. This is where most iteration happens. On macOS the low 4 GB is
  PAGEZERO; link with `-Wl,-pagezero_size,0x1000` for the fixed memory
  window.

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

TypeScript, Node + `ws`. Phase 2 runs it locally. Phase 3 deploys the same
code as a container (Fly.io, Railway or a VPS) behind TLS (D-010; Cloudflare
Durable Objects were dropped: 5 players at 30 Hz is ~1.1M messages per
2-hour session, far past the free tier, and would need a second code path).
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

1. Import: pick a Dolphin-extracted folder (disc images are Phase 5, P5-01;
   Wii images are encrypted and need the user's common key). Run cooker in
   workers. Progress bar. Request persistent storage. Store to OPFS.
2. Lobby: name, character (Mario/Luigi), colour tint for puppets, create
   or join room by code. Or "play solo".
3. Game: canvas, overlay with player list, net status, toasts, pause menu
   hook, settings (resolution scale, controls, volume).

The overlay is HTML on top of the canvas, not in-game UI. Cheaper and it's
where the design work goes.

### 5.6 tools/

- `cook/`: GalaxyQuest's `cook.py` and its ~13 format modules (Python,
  stdlib only, byte-swaps RARC/J3D/KCL/BCSV/layout/font/message/particle/
  JAudio to little-endian; textures stay in GX formats and are decoded at
  load by the renderer). Desktop tool and the oracle.
- `cook-ts/`: the TypeScript port that runs in Web Workers in the browser
  (D-009). Must produce byte-identical output to `cook.py`;
  `tools/cook/compare.py` checks. No Pyodide, no Rust.
- `bench/`: `--headless` runner scripts, the synthetic stub game folder for
  CI, a frame-time capture script, and a baseline file CI compares to.

## 6. Phases

Each phase has an exit gate. Don't start the next phase's non-`parallel-ok`
tasks until the gate passes. Task IDs are in `TASKS.md`.

### Phase 0: Spike (2-4 weeks)

Goal: prove the toolchain end to end.

- Both humans play smg-online in Dolphin for an evening (P0-00). It is the
  model we are building; feel it before building it.
- Vendor the GalaxyQuest snapshot (decomp + platform + cook). Strip
  Android/OpenXR. Two parallel tracks: lane B builds the desktop host
  (memory window, OS/DVD glue, null renderer) while lane A integrates
  Aurora as the GX layer (P0-11, the critical path, 3-week timebox;
  fallback is the WebGL2 rework of GalaxyQuest's renderer, P0-10).
- Get the native build to boot to the title screen with a cooked disc.
- Build the same with Emscripten. Boot to the title screen in Chromium
  with the cooked folder loaded from OPFS. Audio may be silent, input may
  be keyboard only.

Gate: `--headless --frames 600` exits 0 on native and web. Title screen
renders in a browser. One human has seen it. D-005 and D-013 decided with
numbers.

### Phase 1: Single-player in the browser (4-8 weeks)

- In-browser cooker (TypeScript in workers, byte-identical to `cook.py`).
  Import screen for an extracted folder. OPFS storage. Persistent storage
  request.
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

- Decide where co-op progress is saved (D-012, P3-00) before any merge
  code is written.
- Progress event log on the server. Join-in-progress gets the full log.
- Star-get merge into the co-op save. Toast on remote star-get.
- Story flag and galaxy unlock merge. Comet Observatory state.
- "Follow host" warp: a button that takes you to the host's galaxy and
  scenario.
- Deploy the relay as a container.
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

- Disc image import (ISO/RVZ/WBFS) with the user's own common key.
- Firefox and Safari support (their WebGPU, OffscreenCanvas and OPFS
  sync-handle behaviour; test matrix).
- Mobile touch controls.
- WebGL2 fallback renderer (P0-10's rework), only if a browser we care
  about still lacks usable WebGPU by then.
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
- Resident (private) memory of the tab under 512 MB. Not "heap size": with
  D-013's fixed window the wasm memory object is >2.5 GB on paper while
  most pages are never touched. Measure with the browser task manager.
- Import (cook) under 10 min for an extracted EUR disc without movies,
  Phase 1.
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
(`-sOFFSCREENCANVAS_SUPPORT`). The WebGPU device and swap chain are
created on the worker; Chrome supports this. `webcompiled` took a
different path (main thread, JSPI for blocking waits, no
SharedArrayBuffer); GalaxyQuest's OS layer schedules its own threads, so
we may not need JSPI. If Emscripten's offscreen path fights you, fallback:
run the game on the main thread with `emscripten_set_main_loop` and accept
jank during cooking. Decide in Phase 0, record in `DECISIONS.md` (D-005).

**OPFS sync reads.** `FileSystemSyncAccessHandle` is worker-only and gives
real synchronous reads, which is what the DVD layer wants. Open handles are
exclusive per file; cache handles, don't reopen per read.

**Petari is "100% code" but not finished.** Maintainers say data linking is
still in progress. GalaxyQuest reports all 44 galaxies load, so the code is
good enough. Expect edge-case crashes in rarely visited places. Log, don't
block.

**Region.** GalaxyQuest tested EUR and USA; JP and KOR are untested. Petari
matches all four, which doesn't matter for a port: we compile the C++. If a
JP or KOR disc breaks, it is probably a DOL table offset in the cooker. Add
the region to `tools/cook/dol_data.py` and move on.

**The GX layer is Phase 0's real work.** Everything else in Phase 0 is
plumbing. Primary: Aurora on WebGPU (D-014, P0-11). Its open questions are
listed in D-014: baked J3D display lists through Aurora's command
processor, what the cooker must stop byte-swapping, EFB-copy coverage,
frame time at 720p, and whether JSPI is needed. Fallback: GalaxyQuest's
renderer is GLES 3.2 (SSBOs, attrib binding API, program binaries, shared
EGL context) and runs on neither WebGL2 nor macOS GL without the rework in
D-008 (P0-10). Don't try to build it unmodified on a Mac or in a browser.

**Memory map in wasm32.** See D-013. GalaxyQuest's fixed 0x80000000 window
exists to keep `u32` pointer casts valid on 64-bit; wasm32 pointers are
already 32-bit, so the window is a convenience, not a need. Start with it
(`-sMAXIMUM_MEMORY=4GB`), measure resident memory, relocate later if it
hurts. Emscripten has >2 GB pointer caveats (`CAN_ADDRESS_2GB`); if
something odd happens above 0x80000000 in JS glue, that is where to look.

**Wii disc images are encrypted.** Partitions are AES-128-CBC with the Wii
common key. Reading an ISO/RVZ in the browser needs the user's key from
their own Dolphin install. Phase 1 takes an extracted folder instead.

**Puppet memory.** Each puppet loads a Mario or Luigi model into the scene
heap. smg-online shows up to 16 remote Marios inside real Wii memory, so 4
puppets should fit. Still measure; if the game's own allocator fights, put
puppet models in a separate heap the game doesn't manage.

**Animation sync.** `PlayerState` carries an animation id and frame. The
animation set for Mario is large and some animations are blended. Phase 2
accepts visible snapping between animations. Phase 5 can add blend state.

## 10. Testing strategy

- Unit: doctest for `platform/` and `net/` (encoders, interpolators, ring
  buffers, OPFS stubs). vitest for `server/` and `web/`.
- Smoke: `--headless --frames 600` on the stub folder in CI with the null
  renderer. Exit 0 or the PR fails. Be clear what it proves: platform init
  and a clean exit on an invalid disc, not that the game boots. Only a
  human with a cooked disc proves boot.
- Cooker: `compare.py` diffs the TS cooker against `cook.py` byte for byte.
  Synthetic per-format fixtures in vitest; the real-disc comparison is a
  human step logged per release.
- Integration (human): a checklist in `docs/PLAYTEST.md` per phase gate.
  Someone with a cooked disc runs it and pastes results into the task log.
- Net soak: `server/test/soak.ts` spins 5 fake clients at 30 Hz for an
  hour against the relay and asserts no dropped sequence numbers.

## 11. Decision log seed

These were pre-made as D-001 to D-006. The review of 2026-10-12
(`docs/REVIEW_2026-10-12.md`) amended or superseded four of them with
D-007 to D-013; `DECISIONS.md` is authoritative, this list is history.

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
- SMGwebport: https://github.com/T4ctica1YT/SMGwebport (build output only:
  wasm, loader, packed game data; no source, no license, no flags. Its
  `shell.js` shows a workable export surface and that it runs without
  OffscreenCanvas. Do not use or link its data.)
- smg-online: https://github.com/FimeV1/smg-online (design reference)
- sm64coopdx: https://github.com/coop-deluxe/sm64coopdx (sync model
  reference)
- noclip.website: https://github.com/magcius/noclip.website (GX reference)
- Aurora: https://github.com/encounter/aurora (MIT; the GX layer, D-014)
- webcompiled: https://github.com/dynamiteswag81/webcompiled (GPL-3;
  WiiCompiled + Aurora in Chrome via emdawnwebgpu. Read, don't copy.)
- WiiCompiled: https://github.com/pgattic/Wiicompiled (GPL-3; static
  recomp of Mario Kart Wii on Aurora. Read, don't copy.)
- Browser SM64 multiplayer precedents: https://github.com/sm64js/sm64js,
  https://github.com/sh1ftmaker/sm64coopdx,
  https://github.com/metalfox-b/sm64-coop-web
- wasm-dolphin: https://github.com/chuntaoc/wasm-dolphin (why not an
  emulator, D-015)
- coi-serviceworker: https://github.com/gzuidhof/coi-serviceworker
