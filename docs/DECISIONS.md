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
hash: `0a5c6608dc3b4c44a9aa68c2535d1793dc565f02` (2026-09-28; filled by P0-02).
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

## D-014 (2026-10-12, lane A): Aurora on WebGPU is the primary GX layer; the GalaxyQuest WebGL2 rework is the fallback. Amends D-008
What: Phase 0 integrates encounter/aurora (MIT) as the GX implementation
in place of GalaxyQuest's `platform/src/gx/`, rendering through WebGPU in
the browser (emdawnwebgpu) and Dawn (Metal/Vulkan/D3D12) natively. The
rest of GalaxyQuest stays: decomp `TARGET_PC` fixes, `os`, `dvd`, `nand`,
`audio`, `input`, and the cooker. P0-11 is a 3-week timeboxed spike with a
go/no-go; on no-go, P0-10 (data-texture rework of GalaxyQuest's renderer
for WebGL2) resumes.
Why: Aurora is the GX layer AstroCore (the human Petari PC port),
Metaforce, Dusklight and WiiCompiled (Mario Kart Wii) all use; it is
maintained by the decomp-toolkit author. `webcompiled` has already run
WiiCompiled + Aurora in Chrome via Emscripten + emdawnwebgpu at ~60 fps
in races, so "Aurora in a browser" is demonstrated, not hoped. Aurora's
command processor has a per-array little-endian flag ("endianness is
baked into the shader"), so cooked LE vertex data is a supported input,
and its `include/dolphin/` headers stand in for the SDK. It also ships a
THP decoder, DVD via nod (reads ISO/RVZ directly) and CARD. Natively it
sidesteps macOS's GL 4.1 ceiling entirely. Against it: WebGPU-only in the
browser (Chrome/Edge first; Firefox and Safari ship WebGPU but are less
proven for this), Dawn is a heavy dependency, and `webcompiled`'s own
Emscripten glue is GPL-3 (read for approach, never copy; Aurora itself is
MIT).
Spike questions P0-11 must answer: do J3D's baked hardware display lists
pass through Aurora's `process()` unchanged (they should; MKWii's do), does
the cooker need to stop swapping anything Aurora wants big-endian, does
`GXCopyTex`/EFB-copy coverage match what SMG uses, frame time at 720p on
the reference machine, and whether JSPI is needed (GalaxyQuest's OS layer
schedules threads itself, so probably not).
Revisit: P0-11 go/no-go, logged with numbers.

## D-015 (2026-10-12, shared): Routes considered and rejected, with evidence
Emulator in the browser: `wasm-dolphin` (Dolphin via Emscripten, PPC->WASM
JIT) runs Wii discs only on its software rasteriser with no Wii Remote
input; Melee reaches ~68-100% speed on a desktop, nothing near SMG at
60 fps. `gecko`/CubeDeck is interpreter-only. Rejected for performance.
Static recompilation: WiiCompiled's translator is generic but every game
needs its own runtime manifest; setup recompiles the disc on the user's
machine (~30 min, 20 GB, 1.2 GB toolchain) and the output is opaque to
modding. Petari is 100% source, which is strictly better for puppets and
hooks. Rejected, but its Aurora integration is the precedent for D-014.
Wait for AstroCore: no release date, human-only policy, mod API
undisclosed. Watch, don't wait.
Cloud streaming (Dolphin + smg-online on a GPU box, WebRTC to each browser
via neko/selkies): works today, zero port work, costs a GPU VPS per
session and adds 30-80 ms. Not the project, but the right way to playtest
the netcode model before the port exists (P0-00).
Netcode: every browser SM64 multiplayer port uses either a WebSocket relay
(sm64-coop-web, sm64js) or PeerJS/WebRTC P2P (sh1ftmaker's coopdx web).
D-003's relay stands.

## D-016 (2026-10-12, lane B): Desktop macOS builds are x86_64 under Rosetta; the game image sits at 0x98000000 everywhere
What: the game keeps pointers in u32 fields, and GalaxyQuest's GPU front
end treats addresses below 0x80000000 as physical, so the game's image
(code and static data) must load at its fixed slot 0x98000000 next to
MEM1/MEM2. Per target:
- Linux: `-no-pie -Wl,-Ttext-segment=0x98000000`.
- macOS: arm64 cannot do it. Executables must be PIE (`-no_pie` and
  `-image_base` are ignored) and the kernel kills any arm64 binary whose
  `__PAGEZERO` is under 4 GiB (exit 137, tested). x86_64 still allows
  both, so the Mac build is x86_64, linked `-pagezero_size 0x10000
  -no_pie -image_base 0x98000000`, and runs under Rosetta. Verified: image
  at 0x98000000, game boots.
- Web: wasm32 pointers are 32-bit anyway; `-sGLOBAL_BASE=0x98000000` puts
  static data in the same slot (P0-06).
Why not a native arm64 Mac build: it would need a custom Mach-O loader
mapping the game at a fixed address, or relocating everything the GPU
front end treats as physical. Neither is worth it for a dev build when
the shipping target is the browser.
Revisit: if Rosetta is removed from macOS, or if dev builds get slow
enough to matter (Linux VM or the web build are the alternatives).

## D-017 (2026-10-12, lane A): Aurora no-go before the spike; P0-10 (GalaxyQuest renderer on GL 4.1 / WebGL2) is the GX path. Supersedes D-014
What: drop Aurora as the GX layer. Keep GalaxyQuest's whole GX pipeline
(Petari's real SDK GX writes a hardware FIFO; `gpu_cp.cpp` emulates the
command processor; `gx_recorder.cpp` batches; a backend draws) and port
its GL backend to GL 4.1 core (macOS desktop) and OpenGL ES 3.0 / WebGL2
(browser) from one source, per the D-008 recipe.
Why, from reading Aurora at `aa0ca71` and GalaxyQuest's gx/ after
vendoring (neither was visible when D-014 was written):
- Aurora is a high-level GX API. Its `GXSetArray` takes `size` and `le`
  arguments the SDK's does not, so every game/JSystem call site would need
  patching, and Petari's SDK GX (which GalaxyQuest compiles, with fixes)
  would be replaced wholesale.
- GalaxyQuest's renderer works below that, on the hardware command stream,
  and is the one proven across all 44 galaxies. Its TARGET_PC hooks
  (HUD/sky/player markers, camera records, EFB-copy handling) talk to the
  recorder; Aurora would orphan them.
- Aurora has no Emscripten support in-tree. The only browser port
  (webcompiled) is GPL-3; we would redo it from scratch.
- The renderer's backend interface is small (`gpu::Backend`: draw, BP/XF/CP
  writes, EFB copy, TLUT, peek), so the porting surface is two files
  (`gl_renderer.cpp`, `gl_shadergen.cpp`, ~3,000 lines), not a pipeline.
Native windowing comes from SDL3 built from source (the Mac build is
x86_64, D-016, and Homebrew's libraries are arm64).
Revisit: if WebGL2 frame time misses budget at P0-08, a WebGPU backend
behind the same `gpu::Backend` interface is the next step (P5-03), not
Aurora.

## D-018 (2026-10-12, lane A): Puppets follow SMG's own ghost player; animations go over the wire as the game's u32 hash. Phase 2 game-side work may run natively in parallel with Phase 1
What:
- `RemotePlayerPuppet` (P2-05) is modelled on `GhostPlayer`
  (`decomp/src/Game/Player/GhostPlayer.cpp`), the Cosmic Mario/Luigi race
  ghost: a LiveActor with Mario's model, a full-body `XanimePlayer` and an
  upper-body one (carried items), driven by per-frame packets. Take its
  model/animation set-up and `receiveGhostPacket`'s application logic;
  leave out the race manager, demos, power-star and hit-sensor parts.
- PlayerState carries `anim_hash` (u32) instead of `anim_id` (u16):
  capture is `MR::getHashCode(MarioActor::mMarioAnim->getXanimePlayer()->
  getCurrentAnimationName())`, playback is
  `XanimePlayer::changeAnimationByHash`, exactly as the ghost does
  (update flag 0x2000). No animation table to maintain on either side.
  NETCODE.md updated (52 -> 54 bytes forwarded).
- P2-04 (capture) and P2-05 (puppet) are `parallel-ok`: the native build
  plays Good Egg, so they can be built and tested natively (two desktop
  clients and a local relay) while the browser build catches up. They
  still need P2-01/P2-02/P2-03 for the network itself.
Why: Nintendo already solved "draw a second Mario from recorded frames"
for the race ghosts; GalaxyQuest already made that code run on PC.
Waiting for the web build would idle lane A for no technical reason.
Revisit: if puppet animation blending looks wrong, the ghost's weight
tracks (`setAnimationWeight`, flag 0x0010's special cases) are the model.

## D-019 (2026-10-13, lane A): Browser memory map: one wasm32 memory, Wii addresses as-is, no uncached mirrors. Settles D-013
What: the web build keeps the console layout inside a single wasm32 linear
memory: `-sGLOBAL_BASE=0x98000000` puts the game image (static data) in its
D-016 slot; MEM1 at 0x80000000 and MEM2 at 0x90000000 are plain ranges of
that memory (no mmap); host malloc starts after the image and grows upward
(it can never return MEM1/MEM2 addresses, which lie below it); memory is
grown to 0xE0010000 at boot (`-sMAXIMUM_MEMORY=4GB`), the locked cache at
0xE0000000 included. The uncached mirrors (0xC0000000/0xD0000000), which
the native builds alias onto MEM1/MEM2 with shared mmaps, are not mapped:
nothing in the game, JSystem or nw4r uses uncached addresses for RAM (the
only users are SDK GX's hardware registers, which the platform layer
emulates). The low pool (thread stacks) comes from malloc.
Why: wasm32 has no mmap aliasing and no fixed placement, but its pointers
are 32-bit, so the addresses themselves need no translation at all.
Verified in Node with emcc 6.0.12: static data at 0x98000250, memory grown
to 3,584 MB, writes at 0x80000000 / 0x90000000 / 0xE0000000, malloc at
0x98010948. Browsers reserve wasm memory lazily, so untouched pages cost
no RAM; D-013's budget is resident memory, measured at P0-09.
Revisit: if a browser refuses a 3.5 GB memory, move the arenas lower; the
GX/renderer assumptions about >= 0x80000000 would need the same change.

## D-020 (2026-10-13, lane A): Players touching each other (push, bounce off heads): option C, adopted 2026-10-13 at the user's request
Status: adopted, option C (the user asked for player interaction before
P2-08). Built: the puppet carries the ghost's Ride sensor (on only while
shown and receiving), pushes the local Mario (mutual repulsion), lets him
bounce on a stomp or hip drop, and sends Contact 0x12 (NETCODE.md) so the
stomped player flinches (Mario::doFlipWeak, no damage). No star bits.
`SMG_CONTACT=0` turns it off; the lobby toggle (P2-06) is still to do.
Original proposal text follows. The spec (BUILD_SPEC.md §1, §6 Phase 2) says players
"see each other"; nothing says they can touch. This entry argues for a
small, local-authority version after the Phase 2 gate, and against
anything more.

What SMG already does: the puppet's model, `GhostPlayer`, is not
touch-free. It has a Ride sensor (`addHitSensorRide("body", radius 40,
+80 up)`, GhostPlayer.cpp:181) and three behaviours: while racing it
pushes Mario (`attackSensor`: `MR::sendMsgPush`, plus one star bit, 5-frame
cooldown); Mario can trample it (`receiveMsgPlayerAttack`:
`isMsgPlayerTrample`, 5 star bits, 10-frame cooldown; Mario bounces via
`doTrampleJump`); everything else is swallowed (`receiveOtherMsg` true).
Mario's side does the actual work: `MarioActor::receiveMsgPush`
(MarioActorDefensiveMsg.cpp:264) moves Mario out of the sensor's cylinder
himself (`cylinderHitCheck`, `mMario->push(push * 1.1f)`), tries a trample
first, and has a hard-coded exemption for the actor named "ゴーストマリオ"
while jumping or hanging. D-018 left all of this out; the puppet header
says "interacting with nothing". So "can players touch" is not "write
player collision", it is "re-enable the ghost's sensor and decide what the
network does about it".

Why it is not a one-liner:
- Each client sees the others 100 ms late plus half the RTT (NETCODE.md
  "Client behaviour": render at now-100 ms). At any running speed that is
  more than the 40-unit sensor radius, so A touching B's puppet and B
  touching A's puppet are different events at different places and times.
  There is no authority to reconcile them and the spec forbids adding one
  before Phase 4 (SKILL.md §9).
- Pushing works without messages: both clients move their own Mario out
  of the other's puppet (mutual repulsion). It is soft and late, and the
  "pusher" visibly overlaps first. Pushing someone off a ledge works, and
  so does griefing with it: a death costs a life.
- Bouncing is one-sided: A lands on B's puppet, A bounces (own client,
  already correct), B feels nothing unless told. Telling B is a new,
  unreliable message with no game effect beyond a reaction.
- The ghost's trample drops 5 star bits. On a puppet that is a free star
  bit farm (stomp a friend forever). Must go.
- A frozen puppet (no data 0.5 s) would be a solid stale obstacle; a
  hidden one (cutscene, other galaxy) must not push. Sensors must follow
  the visible/frozen state (`validateHitSensors`/`invalidateHitSensors`).
- Standing on someone's head is a different feature. SMG stands Mario on
  collision (KCL `CollisionParts`), never on a sensor; the ghost has none.
  A collision body following a 30 Hz interpolated pose is moving-platform
  work with a stale target. Not for Phase 2 or 3.
- Mario's "scouter" (MarioActorSensor.cpp:374) treats Ride sensors as
  things to look at; check it does not make Mario stare at puppets.

Options:
A. Nothing. Ghosts stay ghosts. Zero desync surface. Matches the spec and
   the "ghost co-op" name. Feels lifeless in a room of five.
B. Local-only touch: the ghost's sensor back on the puppet (push +
   trample, star bits removed, sensor off when frozen/hidden, keep Mario's
   jump/hang exemption by giving the puppet the ghost's actor name or by
   adding our own check under TARGET_PC). No protocol change. ~1-2 days.
   Bounce is one-sided; push is mutual repulsion.
C. B plus a `Contact` message (type 0x12: target_id, kind {trample,
   push}, f16 dir[3]; forwarded to the target only; unreliable; one per
   10 frames per pair). The target's client plays a reaction (a flinch
   animation or a small `mMario->push`, no damage, nothing that changes
   progress). Server stays a relay. ~1 week with tests.
D. Real physics between players (stand on heads, carry, throw, damage).
   Needs collision on puppets, authority and reconciliation. Rejected for
   the same reason as a fully synced world (BUILD_SPEC §2).

Recommendation: do not touch Phase 2's gate with this. Run P2-08 as
specified (ghosts), then ask the three players whether they missed
touching. If yes, take C as a parallel-ok task after P2-08 (P2-09), with
a per-room "contact" toggle in the lobby (P2-06) defaulting on, so a room
that gets griefed can turn it off. B is C's first milestone and is
testable alone with PETARI_LOOPBACK (you push and stomp your own
1-second-old ghost). If no, A, and this entry closes.

Why: the game already contains player-on-actor touch, so the cost is
small, but every touch is a desync visible to both players at once, and
Phase 2's gate is "no desync complaints". Adding touch before the gate
makes the gate measure the wrong thing.
Revisit: when Phase 4 gives the room a host with authority over objects,
C's reactions could become authoritative (host decides who bounced on
whom); and if P5-05 (two players on one machine) ever lands, those two
have zero latency and D is possible for them alone.

## D-021 (2026-10-13, lane A): The browser cooker runs cook.py itself under Pyodide. Supersedes D-009
What: P1-02 does not port `tools/cook/*.py` to TypeScript. `web/cook/` runs
the real `cook.py` in a pool of Web Workers, each with Pyodide 0.26.4 (from
jsDelivr), one disc file per job through `cook.process()`. Yaz0 is decoded
in JS; plain copies, OPFS writes and `manifest.json` (same bytes as
`manifest.py`) are done on the page. Numbered D-021 because an unrelated
D-020 draft exists in another working tree.
Why: D-009 assumed Pyodide would blow the 10-minute budget. Measured: the
EUR disc converts in 36 s with 6 workers in headless Chrome (the CLI takes
10 s), and the output is byte-identical to `cook.py`'s for all 2,375 files,
manifest included. That reverses the trade-off: one cooker to maintain
instead of two kept in lockstep. The only native hot path was Yaz0, now in
JS. Cost: a ~10 MB Pyodide download on first import (cached by the browser).
Revisit: if the jsDelivr dependency matters for a deployed site, serve the
same Pyodide files from our own origin; nothing else changes.

## D-022 (2026-10-13, lane A, decided by the user): Co-op progress merges into the save file you play on. Settles P3-00
What: in a room, power stars, grand stars and the shared story flags that
any player gets are applied to the save file each player has loaded, and
saved with it as usual. No separate co-op save. Progress only ever adds
(a star is never removed by sync).
Why: the user's choice (asked 2026-10-13; the alternatives were a separate
co-op save, or no shared progress yet). One save keeps solo and co-op play
continuous.
Consequence: joining a room can permanently add progress to a solo file.
The lobby says so before joining. Export a save first (settings) to keep
a copy.
Revisit: if players ask to keep solo files untouched, a per-room save slot
is a small change in the launcher (save directory), not in the sync.

## D-023 (2026-10-13, lane A, asked for by the user): Wii menu look for the page, M PLUS 1p as its font, menu music kept out of git
Why: the user wanted the opening screens to look like the Wii menu in the Wii font. That font, Rodin NTLG, is commercial, so the page uses it only when installed and otherwise ships M PLUS 1p (SIL OFL 1.1, the closest open lookalike, Latin subset, 11 KB a weight, `web/fonts/`). OFL is outside AGENTS.md's MIT/BSD/Unlicense/CC0 list, as Inter already was; it allows bundling with the license file. The Wii Menu theme is Nintendo's, so `web/music/*.mp3` is git-ignored: each player drops their own copy there and the page is silent without it.
Revisit: if a permissively licensed Rodin lookalike turns up, or the server should serve the music (server/src/static.ts has no .mp3 or .woff2 type yet).

## D-024 (2026-10-13, lane A, asked for by the user): A dancing Luma on the first-visit loading screen, with three.js, and its model and song kept out of git
Why: the user wanted their Luma model dancing above the loading bar, with Comet Observatory looping a little quieter than the menu music. three.js 0.169.0 (MIT) is vendored in `web/vendor/three/` (the core build, FBXLoader and what it imports) and loaded only on that screen. The model (`web/luma/luma.fbx`, a fan Sketchfab model of a Nintendo character, no license given) and both songs (Nintendo's) are in git because the user committed them (221ff706f), superseding D-023's ignore rule for `web/music/`. The page still works without them: no Luma, no music. `?demo=loading` shows the screen with a fake bar.
Revisit: the repo is public, so these files are published once pushed; or if the relay's static hosting should serve these files (server/src/static.ts has no .mp3 or .fbx type).

## D-025 (2026-10-14, lane A, decided by the user): The room takes its host's progress. Extends D-022
What: the room's host (its owner: whoever made it, or the next player when
they leave) is the source of progress from before the room. Each client
uploads its current file (the game's own save format, ~1.8 KB); the relay
passes the host's to everyone else. An existing save gets the stars, shared
flags and story progress it lacks (adds only); a brand-new file started in
the room becomes a copy of the host's and skips the prologue. Progress made
in the room is still shared by everyone, as in D-022.
Why: the user's choice (asked 2026-10-14; the alternative was everyone's
saves combined, so one friend's full save would fill everyone's). Lets a
newcomer drop in at any time.
Revisit: if players want to keep a fresh file (SMG_DROP_IN=0 for now).

