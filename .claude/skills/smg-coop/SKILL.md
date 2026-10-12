---
name: smg-coop
description: Working skill for the browser Super Mario Galaxy co-op port. Load at the start of any session in this repo. Covers the architecture, the single-player assumptions in the SMG code you will hit, the netcode model, how to build and test, and the traps that waste time. Use for any task in platform/, net/, server/, web/, tools/ or decomp/.
---

# SMG Co-op skill

This skill is shared by Claude Code and Gemini CLI. It is the project's
working knowledge. `AGENTS.md` has the process rules; this has the
technical ones.

## 1. The architecture in one paragraph

Petari (the SMG decomp, C++) is compiled with Emscripten to WebAssembly. It
does not know it is in a browser. A platform layer in `platform/` implements
the Wii SDK surface the game calls: GX (graphics), AX/DSP (audio), WPAD
(controllers), DVD (disc reads), NAND (saves), OS (threads, timers, memory).
GalaxyQuest already wrote most of this for Android/GLES; we are porting that
layer to the web, not writing it from scratch. The web shell in `web/` owns
the canvas, file import, lobby and settings. For multiplayer, each client
runs the full game locally; a `net/` module sends the local player's pose
and receives other players' poses, which drive puppet actors in the game.
A relay server in `server/` forwards poses and keeps the shared progress
log.

## 2. Where things come from

| Piece | Source | How we use it |
|---|---|---|
| Game code | Petari via GalaxyQuest `decomp/` (has `TARGET_PC` fixes) | Vendored. Diff against `petari-base` tag to see port changes. |
| GX -> GLES renderer | GalaxyQuest `platform/` | Port GLES3 calls to WebGL2. Same API family; mostly build-system work plus texture/readback gaps. |
| DSP audio mixer | GalaxyQuest `platform/` | Keep. Output buffer goes to an AudioWorklet instead of AAudio. |
| Asset cooker | GalaxyQuest `tools/cook/cook.py` | Port to run in-browser (Pyodide for Phase 1, then a JS/WASM rewrite). Reads `sys/main.dol` too. |
| Emscripten flags, COI service worker | SMGwebport | Read their `shell.js` and `index.html`. Do not copy their `.data` files. |
| Netcode model | smg-online, sm64coopdx | Design only. Headpenguin's code is unlicensed. |
| GX shader reference | noclip.website `src/gx/` | Reference for TEV accuracy when our output looks wrong. Check license before copying. |

## 3. Build and run

```
# 1. native desktop build. Use this for 90% of iteration. Fast, debuggable.
cmake -B build/native -G Ninja -DSMG_TARGET=native -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build/native
./build/native/smg --game ./game --galaxy EggStarGalaxy --headless --frames 600

# 2. web build
emcmake cmake -B build/web -G Ninja -DSMG_TARGET=web -DCMAKE_BUILD_TYPE=Release
ninja -C build/web
cd web && npm run dev
# open http://localhost:5173 in Chromium. Import your cooked game folder.

# 3. tests
ninja -C build/native test
cd server && npm test
cd web && npm test

# 4. relay server (local)
cd server && npm run dev   # ws://localhost:8787
```

`--headless --frames N` runs the game loop N frames with no window and
exits 0 if nothing crashed. It is the smoke test. Every PR runs it in CI
against a synthetic stub game folder (`tools/bench/stub-game/`), which has
the file layout but no real assets, so it only proves boot, not rendering.
Real rendering tests need a human with a cooked disc.

## 4. Game data

The game needs a cooked copy of the user's own disc. Never in git.

```
# once, as a human, with Dolphin: extract the disc to ./extracted
python tools/cook/cook.py extracted game --with-movies   # ~3.3 GB
python tools/cook/cook.py extracted game                 # ~1 GB, movies black
```

In the browser the same cooking runs client-side (Phase 1 task P1-02) and
the result lands in OPFS under `/smg/game/`. The web shell shows an import
screen if OPFS is empty.

Tested disc: RMGP01 (EUR). Petari's matching target is RMGK01 (KOR). The
GalaxyQuest cooker reads a few tables from `main.dol` so region
differences mostly wash out, but if something region-specific breaks, check
the dol offsets in `tools/cook/dol_tables.py` first.

## 5. Single-player assumptions you will hit

The game was written for one Mario. Do not try to make it support N
Marios. For online play, remote players are **puppets**: a model, an
animation player and a transform, driven by network data. They are not
`MarioActor`. They do not run physics, collect anything or trigger
anything.

Things that assume one player, and what we do about them:

| System | Assumption | Our rule |
|---|---|---|
| Player singleton (`MarioActor` via the player holder) | One instance, global accessor | Leave it. Puppets are a new actor class, `RemotePlayerPuppet`, registered like any NPC. |
| Camera director | Follows the one player | Leave it. Each client's camera follows its own player. |
| Gravity | Computed for the one player's position | Puppets copy their up-vector from the network, don't compute it. |
| Star get / demo sequences | Freezes the world, plays a cutscene for the one player | Local only. When a remote player gets a star, show a toast, don't freeze. |
| Stage/scene transition | Tears down everything, including anything we spawned | Puppets re-spawn on scene enter from the current net state. Net module survives transitions (lives outside the scene heap). |
| Save data | One NAND file, written by the one player | Shared progress events are merged into the local save on receipt. Server keeps the canonical log. |
| HUD counters | Read from the one player | Leave it. Remote counts go in the web overlay, not the in-game HUD. |
| Heap layout | Fixed-size heaps, Wii memory map | Puppets allocate from the scene heap like NPCs. Budget for 4 puppets' models. Measure. |

Where to look in `decomp/`: `src/Game/Player/` (Mario), `src/Game/Camera/`,
`src/Game/Scene/`, `src/Game/System/` (game data, save), `src/Game/LiveActor/`
(the actor base class puppets will extend), `src/Game/NPC/` (how NPCs with
animation players are set up; copy the pattern). Exact class names are not
verified in the spec. Grep before you assume.

## 6. Netcode model (short form; full protocol in docs/NETCODE.md)

- Transport: WebSocket to the relay. Binary frames. No WebRTC in Phases 2-3.
- Each client sends its own `PlayerState` at 30 Hz: position (3x f32),
  quaternion (4x f16), up-vector (3x f16), velocity (3x f16), anim id (u16),
  anim frame (f16), flags (u16: character, powerup, grounded, spinning,
  visible), galaxy id (u16), scenario id (u8). About 48 bytes.
- The relay forwards each state to every other client in the same room. It
  does not simulate anything.
- Clients buffer remote states and render puppets 100 ms behind, with
  Hermite interpolation between the two nearest samples. On a gap longer
  than 500 ms, freeze the puppet. On 3 s, hide it.
- Puppets are only drawn when galaxy id and scenario id match the local
  player's.
- Progress events (`StarGet`, `StoryFlag`, `GalaxyUnlock`) are reliable
  messages with a monotonic sequence number per room. The server appends
  to a per-room log and sends the full log to joiners. Clients ack by
  sequence. Merging is union: nobody loses a star.
- No rollback. No lockstep. No shared enemies until Phase 4.

## 7. Known traps

- **Emscripten pthreads**: needs `-pthread`, `-sPTHREAD_POOL_SIZE`,
  `SharedArrayBuffer`, which needs the page to be cross-origin isolated
  (COOP/COEP headers). Vite dev config sets them. GitHub Pages can't; use
  `coi-serviceworker.js` there. If `SharedArrayBuffer is not defined`,
  this is why.
- **Main-thread blocking**: the game loop can't block the browser main
  thread. Run the game on a worker (`-sPROXY_TO_PTHREAD`) and use
  OffscreenCanvas for WebGL2. Safari OffscreenCanvas + WebGL2 support is
  recent; don't target Safari until Phase 5.
- **GX readback**: GX features that read the framebuffer back (lens flare,
  some screen effects) don't work in GalaxyQuest's renderer either. Stub
  them. Don't spend a week on the sun's lens flare.
- **Endianness**: cooked data is little-endian. If you read raw disc
  formats anywhere outside the cooker, you're doing it wrong.
- **Texture formats**: GX texture formats (CMPR, RGB5A3, I8, IA4 etc.) are
  decoded at cook time to RGBA8 or BC1/BC3. WebGL2 BC1 needs
  `WEBGL_compressed_texture_s3tc`; check it exists, fall back to RGBA8.
- **Shader compile stalls**: GX TEV configs compile to many shader
  variants. First visit to a galaxy will hitch. Pre-seed a shader cache
  (SMGwebport ships `shader-seed.bin`). Phase 1 task.
- **OPFS quota**: 3.3 GB is over the default quota on some browsers.
  Ask for persistent storage (`navigator.storage.persist()`). Ship without
  movies by default (1 GB).
- **Memory**: the Wii has 88 MB. Emscripten heap should be 256 MB
  (`-sINITIAL_MEMORY`) with growth on. Puppet models add to this. Measure.
- **Audio**: AudioWorklet needs a user gesture to start. The import/lobby
  screen click is that gesture. Keep the AudioContext alive across scene
  changes.
- **Two agents, one `CMakeLists.txt`**: it is shared. Add your sources via
  per-directory `CMakeLists.txt` that the root includes, so two PRs don't
  conflict on the same file.

## 8. Definition of done for a task

- Code builds in native and web targets (or the task says which).
- Tests added or updated. CI green.
- `--headless --frames 600` still exits 0.
- `TASKS.md` updated in the PR. `log` has a dated entry.
- If you changed a decision or added a dependency, `DECISIONS.md` has it.
- PR body says how a reviewer can see the change working.

## 9. Things not to do

- Don't bundle game data. Don't hard-code a path to someone's game folder.
- Don't add WebRTC, WebTransport, rollback or a physics sync before
  Phase 4. The spec says WebSocket + puppets. Prove that works first.
- Don't touch the matching Wii build (`configure.py`, `orig/`,
  `config/RMGK01/`). We don't build for Wii. Leave Petari's own tooling
  alone so we can rebase on upstream.
- Don't open PRs against Petari or AstroCore.
- Don't optimise before the benchmark in `tools/bench/` says you need to.
