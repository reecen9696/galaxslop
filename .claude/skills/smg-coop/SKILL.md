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
GalaxyQuest already wrote most of this for Android; we port its OS, disc,
save, audio and input layers to the web rather than writing them from
scratch. Graphics are different: GX goes to Aurora (`extern/aurora`), the
MIT GX implementation AstroCore and other decomp ports use, which renders
through WebGPU in the browser and Dawn natively. The web shell in `web/` owns
the canvas, file import, lobby and settings. For multiplayer, each client
runs the full game locally; a `net/` module sends the local player's pose
and receives other players' poses, which drive puppet actors in the game.
A relay server in `server/` forwards poses and keeps the shared progress
log.

## 2. Where things come from

| Piece | Source | How we use it |
|---|---|---|
| Game code | Petari via GalaxyQuest `decomp/` (has `TARGET_PC` fixes) | Vendored as one snapshot with `platform/` and `tools/cook/` at GalaxyQuest `d8a039c` (D-007). Diff against `petari-base` tag to see port changes. Until P0-02 lands, `decomp/` is raw Petari. |
| GX layer (primary) | encounter/aurora (MIT) | Link `aurora::gx` in place of the SDK's GX and GalaxyQuest's `gx/`. WebGPU in the browser via emdawnwebgpu, Dawn natively. Same layer AstroCore, Metaforce and WiiCompiled use; `webcompiled` runs it in Chrome at 60 fps. Has a per-array little-endian flag for cooked data. P0-11 is the spike (D-014). |
| GX layer (fallback) | GalaxyQuest `platform/src/gx/` | Only if P0-11 fails its timebox. It is GLES 3.2 (SSBOs, attrib binding API, program binaries); P0-10 reworks it to ES 3.0 / WebGL2 (D-008). |
| DSP audio mixer | GalaxyQuest `platform/src/audio/` | Keep. Output buffer goes to an AudioWorklet instead of AAudio. |
| Asset cooker | GalaxyQuest `tools/cook/cook.py` | Desktop tool and oracle. The browser runs a TypeScript port in workers (D-009) that must match it byte for byte. Input is an extracted folder; it does not read ISOs. |
| Emscripten precedent | SMGwebport | Proof it runs in a browser, nothing more: build output only, no source, no license, no flags. Its `shell.js` shows an export surface (`web_audio_ring`, `web_input_update`, `web_shader_warmup`) and that it works without OffscreenCanvas. Do not copy or link its data. |
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
against a synthetic stub game folder (`tools/bench/stub-game/`) with the
null renderer (`-DSMG_GX=null`). The stub has the file layout but no real
assets, so this proves platform init and a clean exit on a bad disc. It
does not prove boot. Only a human with a cooked disc proves boot.

## 4. Game data

The game needs a cooked copy of the user's own disc. Never in git.

```
# once, as a human, with Dolphin: extract the disc to ./extracted
python tools/cook/cook.py extracted game --with-movies   # ~3.3 GB
python tools/cook/cook.py extracted game                 # ~1 GB, movies black
```

`cook.py` takes an extracted folder only (`sys/fst.bin`, `sys/main.dol`,
`files/`). It byte-swaps formats to little-endian; textures stay in GX
formats and the renderer decodes them at load. It also pulls
`GalaxyID.bcsv`, `StoryEvent.bcsv` and `ErrorMessageArchive.arc` out of the
user's `main.dol` into `<game>/sys/`; those three never live in git (D-011).

In the browser a TypeScript port of the same cooker runs in Web Workers
(P1-02, D-009) on a folder the user picks, and the result lands in OPFS
under `/smg/game/`. Disc images (ISO/RVZ) are Phase 5: Wii partitions are
encrypted and need the user's own common key. The web shell shows an
import screen if OPFS is empty.

Tested discs: RMGP01 (EUR) and RMGE01 (USA) by GalaxyQuest. JP and KOR are
untested. The cooker reads a few tables from `main.dol`; if something
region-specific breaks, check `tools/cook/dol_data.py` first.

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
| Save data | One NAND file, written by the one player | Shared progress events are merged into a co-op save on receipt, never into solo slots (D-012, decided in P3-00). Server keeps the canonical log. |
| HUD counters | Read from the one player | Leave it. Remote counts go in the web overlay, not the in-game HUD. |
| Heap layout | Fixed-size heaps, Wii memory map | Puppets allocate from the scene heap like NPCs. smg-online fits 16 remote Marios in real Wii memory, so 4 should fit. Measure anyway. |

Where to look in `decomp/`: `src/Game/Player/` (Mario), `src/Game/Camera/`,
`src/Game/Scene/`, `src/Game/System/` (game data, save), `src/Game/LiveActor/`
(the actor base class puppets will extend), `src/Game/NPC/` (how NPCs with
animation players are set up; copy the pattern). Exact class names are not
verified in the spec. Grep before you assume.

## 6. Netcode model (short form; full protocol in docs/NETCODE.md)

- Transport: WebSocket to the relay. Binary frames. No WebRTC in Phases 2-3.
- Each client sends its own `PlayerState` at 30 Hz: position (3x f32),
  quaternion (4x f16), up-vector (3x f16), velocity (3x f16), anim hash (u32, the game's own; D-018),
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
  thread, and OPFS sync reads only work on a worker. Run the game on a
  pthread (`-sPROXY_TO_PTHREAD`) with OffscreenCanvas and the WebGPU
  device created on the worker (D-005). SMGwebport runs without
  OffscreenCanvas and `webcompiled` runs Aurora on the main thread with
  JSPI, so the main-thread fallback is known to work if the offscreen path
  fights you. Firefox and Safari are Phase 5.
- **Aurora is the GX layer, not GalaxyQuest's renderer**: link
  `aurora::gx`; don't compile `decomp/src/RVL_SDK/gx` or GalaxyQuest's
  `gl_renderer.cpp`. Aurora wants hardware-format (big-endian) display
  lists and tells the shader per vertex array whether the data is
  little-endian; P0-11 settles what the cooker must leave alone. Aurora's
  `include/dolphin/` headers replace `<revolution/gx.h>` for GX only.
- **GalaxyQuest's renderer is GLES 3.2**: `#version 320 es`, four SSBOs,
  `glVertexAttribFormat`, `glProgramBinary`, shared-context compile
  thread. WebGL2 (ES 3.0) and macOS GL (4.1) have none of them. It is the
  fallback (P0-10), not the plan. Don't try to build it unmodified on a
  Mac or in a browser.
- **GX readback**: `GXPeekARGB`/`GXPeekZ` are unsupported in GalaxyQuest's
  renderer too. Stub them. The sun's lens flare never appears. Don't spend
  a week on it.
- **Endianness**: cooked data is little-endian. If you read raw disc
  formats anywhere outside the cooker, you're doing it wrong.
- **Texture formats**: the cooker does not decode textures. GX formats
  (CMPR, RGB5A3, I8, IA4...) reach the renderer byte-swapped and are
  decoded to RGBA8 at load (`TexImage`). No S3TC extension needed. THP
  video decodes to I8 planes and TEV does the YUV->RGB.
- **Shader compile stalls**: GX TEV configs compile to many pipeline
  variants. First visit to a galaxy will hitch. Aurora has a pipeline
  cache with transferable entries; persist it to OPFS and ship a seed
  built by a headless tour (P1-06). WebGPU pipeline creation is async
  (`createRenderPipelineAsync`); never block the frame on it. SMGwebport's
  seed is for a different shader generator; useless to us.
- **OPFS quota**: 3.3 GB is over the default quota on some browsers.
  Ask for persistent storage (`navigator.storage.persist()`). Ship without
  movies by default (1 GB).
- **Memory**: the Wii has 88 MB. GalaxyQuest maps MEM1/MEM2 at
  0x80000000/0x90000000 (D-013), so the wasm memory object is >2.5 GB on
  paper: `-sMAXIMUM_MEMORY=4GB`, growth on. Pages are committed lazily;
  judge by the tab's resident memory, not `buffer.byteLength`. Puppet
  models add to this. Measure.
- **Threads**: GalaxyQuest emulates OS threads in its own scheduler. The
  game is single-threaded; real threads are the frame builder and audio.
  `PTHREAD_POOL_SIZE` is small. Don't map OSThread onto pthreads.
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
