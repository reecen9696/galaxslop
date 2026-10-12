# SMG Co-op (browser)

Super Mario Galaxy, running in a web browser, with up to 5 players online.
Personal research project. Built on the Petari decompilation.

Two people work on this at the same time, each driving an AI agent:
Reece with Claude Code, and a friend with Gemini CLI. Both agents read the
same rules in `AGENTS.md` and the same skill in `skills/smg-coop/SKILL.md`,
and both work off the same board in `TASKS.md`.

## Read this first

**The decompiled code in this repo does not contain the game.** Petari is
source code only: no models, textures, levels, music or text. The game only
runs when it has the data from a Super Mario Galaxy disc. That data is never
committed here. Each person extracts their own disc once, cooks it with the
tool in `tools/cook/`, and keeps the result in `game/` (gitignored). The
browser build loads it from local storage on the user's machine.

If you don't have a disc dump yet, you can still do a lot of the work:
everything in the platform layer, the renderer, the build system, the net
code and the web shell can be written and unit-tested without game data.
But nothing can be run end to end until a cooked `game/` folder exists.
Get one before Phase 1 ends.

## What this is

- A native port of SMG (not an emulator). Petari's C++ runs as WebAssembly.
- A platform layer that replaces the Wii: GX graphics to WebGL2, DSP audio
  to an AudioWorklet, Wii Remote to gamepad and mouse, disc reads to the
  browser's Origin Private File System, NAND saves to IndexedDB.
- Online co-op on the "independent worlds" model used by every working
  Mario online mod: each player runs their own full copy of the game, other
  players appear as puppet Marios/Luigis, and progress (stars, story flags)
  is shared through a small relay server.

## What this is not

- Not an emulator, not Dolphin-in-WASM.
- Not a synced world. Enemies, coins and objects are local to each client
  in Phases 1-3. Phase 4 syncs a short list of objects, host-authoritative.
- Not split-screen. Each client has its own camera on its own player.
- Not a contribution to Petari or AstroCore. Both are human-only projects.
  This repo is AI-assisted and says so. Don't open PRs upstream from here.

## Repo layout

```
AGENTS.md              rules for both AI agents (Claude and Gemini)
CLAUDE.md              points Claude Code at AGENTS.md and the skill
GEMINI.md              points Gemini CLI at AGENTS.md and the skill
TASKS.md               the live task board. Claim, work, update, commit.
skills/smg-coop/       the shared skill both agents load
  SKILL.md
docs/
  BUILD_SPEC.md        full technical spec and phased plan
  NETCODE.md           multiplayer protocol
  DECISIONS.md         architecture decision log (append-only)
  REVIEW_2026-10-12.md the review that produced D-007..D-013
decomp/                Petari sources + TARGET_PC fixes (GalaxyQuest snapshot)
platform/              the Wii replacement layer (C++), GalaxyQuest layout
  include/
  src/
    gx/                GX FIFO -> WebGL2 / GL 3.3 (after the ES 3.0 rework)
    audio/             DSP mixer -> AudioWorklet / SDL
    input/             WPAD <- Gamepad API / mouse / keyboard
    dvd/  nand/        disc reads <- OPFS, saves -> IndexedDB
    os/                emulated OS threads, timers, memory window
    sdk/  port/        THP decoder, misc SDK, port glue
    native/            SDL2 desktop host
    web/               Emscripten host glue
net/                   client-side netcode (C++ + JS bridge)
server/                relay server (TypeScript, WebSocket, container)
web/                   the web shell: file import, lobby, canvas, HUD overlay
tools/
  cook/                extracted disc -> cooked layout (Python, the oracle)
  cook-ts/             the same cooker in TypeScript for the browser
  bench/               performance and smoke-test scripts
game/                  YOUR cooked disc. gitignored. never commit.
```

State on 2026-10-12: docs and board exist; `decomp/` holds raw Petari until
P0-02 swaps in the GalaxyQuest snapshot; `platform/` is empty dirs.

## Getting started (either agent)

1. Clone. Read `AGENTS.md`, then `skills/smg-coop/SKILL.md`, then
   `docs/BUILD_SPEC.md`, then `docs/REVIEW_2026-10-12.md` once.
2. Open `TASKS.md`. Pick an unclaimed task in the earliest open phase that
   matches your lane (Claude: lane A, Gemini: lane B, unless the board says
   otherwise). Claim it by editing the `owner` and `status` fields and
   committing that change before writing any code.
3. Work on a branch named `<lane>/<task-id>-<short-name>`, e.g.
   `a/P0-03-emscripten-boot`.
4. When done, update the task to `review`, push, open a PR against `main`.
   The other person's agent reviews. Merge when green.

## Prerequisites

- Emscripten SDK (emsdk) 3.1.60+ with pthreads support
- Clang with C++20; CMake 3.24+, Ninja
- SDL2 (native build)
- Node 20+ (web shell, relay server, tests)
- Python 3.10+ (cook tool)
- Dolphin (to extract the disc) - only needed once, by a human
- A Chromium browser for testing (WebGL2, SharedArrayBuffer, OPFS)

## Build

```
# native desktop (fast iteration, no browser)
cmake -B build/native -G Ninja -DSMG_TARGET=native
ninja -C build/native

# web
emcmake cmake -B build/web -G Ninja -DSMG_TARGET=web
ninja -C build/web
cd web && npm install && npm run dev   # serves with COOP/COEP headers
```

See `docs/BUILD_SPEC.md` for the full flow, including cooking the disc.

## Legal

- Petari: CC0-1.0. GalaxyQuest platform code: Unlicense. This repo's own
  code: Unlicense. See `THIRD_PARTY_NOTICES.md`.
- No Nintendo data is in this repo, in any build artifact, or on any server
  this project runs. Game data is loaded client-side from the user's own
  files and stays on their machine.
- Super Mario Galaxy and Mario are Nintendo trademarks. This is an
  unofficial fan research project.

## Credits

Petari (SMGCommunity), GalaxyQuest (bigmak94), smg-online (FimeV1,
Headpenguin), noclip.website (magcius), Dolphin, and sm64coopdx for the
netcode model.
