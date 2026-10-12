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
