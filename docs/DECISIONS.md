# Decision log

Append-only. Numbered. Dated. Lane that made it. Three lines minimum:
what, why, when to revisit. Never edit an old entry; supersede it.

## D-001 (2026-10-12, shared): Vendor decomp from GalaxyQuest, not Petari
What: `decomp/` is copied from bigmak94/GalaxyQuest `decomp/` at commit
`<FILL IN ON VENDOR>`, tagged `petari-base`.
Why: it already has `TARGET_PC` little-endian and 64-bit fixes. Starting
from raw Petari repeats weeks of work.
Revisit: if GalaxyQuest goes stale and Petari upstream fixes matter, rebase.

## D-002 (2026-10-12, shared): WebGL2 for Phases 0-4
What: the GX layer targets WebGL2. No WebGPU.
Why: GalaxyQuest's GLES3 renderer maps almost 1:1. WebGPU support in
Safari is behind flags; Aurora has no maintained browser target.
Revisit: Phase 5, as a benchmarked spike.

## D-003 (2026-10-12, shared): WebSocket relay, snapshot interpolation
What: one WebSocket per client to a relay. 30 Hz PlayerState. Puppets
render 100 ms behind. No rollback, no lockstep, no WebRTC.
Why: bandwidth is ~7 KB/s per client. Rollback needs cheap full-state
snapshots the game can't give. Every working SMG/Sunshine online mod uses
this model.
Revisit: Phase 4 if object sync needs lower latency than TCP gives.

## D-004 (2026-10-12, shared): Puppets, not player actors, for remote players
What: `RemotePlayerPuppet` is a `LiveActor` with a model and animation
player, driven by network data. It is not `MarioActor`.
Why: the game assumes one player in camera, gravity, cutscenes, save,
HUD and heap. A second real player actor touches all of them.
Revisit: Phase 5 local 2-player, which does need a second real player.

## D-005 (2026-10-12, shared): Game on a pthread with OffscreenCanvas
What: `-sPROXY_TO_PTHREAD -sOFFSCREENCANVAS_SUPPORT`. Main thread owns UI,
audio, input, WebSocket. Game thread owns GL.
Why: in-browser cooking and page UI can't jank the game loop, and OPFS
sync reads are worker-only.
Revisit: end of Phase 0 if Emscripten's offscreen path is unstable; the
fallback is main-thread `emscripten_set_main_loop`.

## D-006 (2026-10-12, shared): Pyodide for the in-browser cooker
What: Phase 1 runs GalaxyQuest's `cook.py` unchanged under Pyodide in a
worker.
Why: correctness first. A Rust/WASM port is a Phase 5 task if import time
fails the 10 min budget.
Revisit: Phase 1 gate.

## D-007 (2026-10-12, shared): Vendor one GalaxyQuest snapshot (decomp + platform + cook). Supersedes D-001
What: `decomp/`, `platform/` and `tools/cook/` come from bigmak94/GalaxyQuest
at `d8a039c96f89c188fc44cdb7382ab9a4cf9ce92e` (0.1.8, 2026-10-03) as one
snapshot. Not raw Petari (what sits in `decomp/` today: Petari `8c5307d`),
and not GalaxyQuest's decomp paired with newer Petari.
Why: the platform layer and the `TARGET_PC` decomp changes were built
together (`platform/include/port/compat.h` is force-included, hooks call
platform symbols). Mixing versions adds a variable to a spike that has
enough. Petari's commits after 100% (2026-09-28) are cleanup; none are
needed to run.
How: replace the contents of `decomp/` in one commit and keep Petari's
upstream history, which is already in this repo, for later rebases. Find
the Petari commit GalaxyQuest started from by matching its `petari-base`
tree hash against our Petari history and move our `petari-base` tag there,
so `git diff petari-base -- decomp` shows only port changes. Petari base
hash: `<FILL IN P0-02>`.
Revisit: if a Petari fix after the snapshot matters, rebase as its own task.

## D-008 (2026-10-12, lane A): The renderer needs an ES 3.0 rework before WebGL2 or macOS. Amends D-002
What: WebGL2 stays the target, but D-002's premise ("maps almost 1:1") is
wrong. GalaxyQuest's renderer is GLES 3.2: `#version 320 es`, four shader
storage buffers carrying per-draw state (`rows`, `recs`, `st`, `vtxRec`
indexed by `gl_VertexID`), `glVertexAttribFormat/Binding`,
`glProgramBinary` for the shader cache, and a compile thread on a shared
EGL context. None of that exists in WebGL2 (ES 3.0). SSBOs also don't exist
in macOS OpenGL (4.1), so the native build on a Mac hits the same wall.
Why keep WebGL2: the fix is mechanical and one renderer then serves both
targets. SSBO reads become `texelFetch` from RGBA32F/RGBA32UI data
textures; `vtxRec[gl_VertexID]` becomes an integer vertex attribute; the
binding API becomes `glVertexAttribPointer`; the binary cache becomes a
GLSL source cache plus `KHR_parallel_shader_compile`. A WebGPU renderer
would fit the SSBO design better, but it means a WGSL generator and a
second renderer for native (Dawn/wgpu). D-002's Safari claim is stale
(WebGPU shipped in Safari 26 and Firefox 141 during 2025); that changes
nothing for Phase 0 and makes P5-03 more credible.
Revisit: after P0-08 measures frame time. If data-texture fetches cost
more than ~2 ms at 720p, pull P5-03 forward.

## D-009 (2026-10-12, lane B): TypeScript cooker in Web Workers. Supersedes D-006
What: the in-browser cooker is a TypeScript port of `tools/cook/*.py`
running in Web Workers and writing to OPFS. `cook.py` stays as the desktop
tool and as the oracle: both must produce byte-identical output for the
same extracted disc. No Pyodide.
Why: `cook.py` uses a `ProcessPoolExecutor` and the port added
`tools/cook/native/` for hot paths; pure Python was already too slow for
them. Pyodide is single-threaded per worker, several times slower than
CPython, and a 10+ MB download. The 10-minute budget was never realistic
that way. The Python is ~90 KB of `struct` byte-swapping with no
third-party imports; a port is tractable, and byte comparison makes it
testable without judgement.
Also: Phase 1 input is a Dolphin-extracted folder picked with
`showDirectoryPicker()`, which is all `cook.py` accepts anyway. ISO/RVZ/
WBFS import is a separate later task: Wii partitions are AES-encrypted, so
a disc-image reader needs the user's own common key (from their Dolphin
install) plus RVZ decompression. Not Phase 1.
Revisit: never for Pyodide. P5-01 (Rust cooker) is gone; optimise the TS
cooker if a profile says to.

## D-010 (2026-10-12, lane B): Relay deploys as a Node container, not Durable Objects. Amends D-003 and BUILD_SPEC 5.4
What: one server codebase (Node + `ws`) for local and production.
Production is a container on a cheap host (Fly.io, Railway, a VPS) behind
TLS.
Why: "free tier is enough" was wrong. 5 players at 30 Hz is ~150 relayed
messages/s, ~1.1M per 2-hour session; the Workers free tier is 100k
requests/day. Paid is cheap (~$0.20 a session) but needs a Durable Object
port and a second code path. A container costs the same and keeps one.
Revisit: if room count ever justifies it.

## D-011 (2026-10-12, shared): The two `.bcsv` tables and the error archive are game data; load them from the cooked disc
What: `decomp/src/Game/System/GalaxyID.bcsv` and `StoryEvent.bcsv` (used
only by `BCSVInclude.s`, the Wii asm build) must not be in `decomp/`.
`cook.py` already extracts both, plus `ErrorMessageArchive.arc`, from the
user's `main.dol` into `<game>/sys/`, and GalaxyQuest's DVD layer loads
them from there. GalaxyQuest excluded them from its repo for this reason;
taking its snapshot (D-007) removes them. P0-02 confirms.
Why: AGENTS.md bans `.bcsv` in git and the tables come from the DOL.
Petari ships them under CC0; that is Petari's call, not ours.
Revisit: no.

## D-012 PROPOSAL (2026-10-12, shared): Co-op progress lives in its own save. Decide before P3-02
What: progress merged from a room must not write into a player's
single-player save slots. Proposal: a per-room (or one "co-op") save
namespace in the NAND emulation, chosen in the lobby; solo play uses the
normal slots.
Why: merging a friend's 60 stars into your solo file is irreversible with
union semantics. smg-online scopes sharing to "from the Observatory on" for
a related reason. Not decided because it needs a look at how the save layer
picks a file. P3-00 turns this into a decision.

## D-013 (2026-10-12, lane A): wasm32 memory map: start with GalaxyQuest's fixed window, measure, then decide
What: GalaxyQuest maps MEM1 at 0x80000000, MEM2 at 0x90000000 and loads
the game library at 0x98000000 inside a reserved low-4 GB window so `u32`
pointer casts in the decomp stay valid on 64-bit. In wasm32 pointers are
already 32-bit, so that motivation goes away, but the OS layer and some
game code may still assume those ranges. Phase 0 builds with the same
fixed addresses and `-sMAXIMUM_MEMORY=4GB`; browsers commit wasm pages
lazily, so measure resident memory, not heap size. If resident memory or
the >2 GB pointer path is a problem, relocate the arenas as a follow-up,
using `tools/find_ptr_casts.py` as the audit. On native macOS the low 4 GB
is PAGEZERO by default; link with `-Wl,-pagezero_size,0x1000`.
Why: a spike changes one thing at a time. Copy the working layout first.
Revisit: P0-09.
