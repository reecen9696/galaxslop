# Task board

Single source of truth. Edit it to claim, progress, block and finish work.
Rules are in `AGENTS.md` section 1. Short version: claim on `main` first,
one task per branch, update this file in the same PR as the code.

Fields:
- `id`: phase + number. Stable. Never renumber.
- `lane`: A (Claude by default), B (Gemini by default), shared.
- `status`: open | in_progress | review | blocked | paused | done
- `owner`: lane letter once claimed. Empty while open.
- `blocked_by`: task ids that must be `done` first.
- `touches`: directories or files. Two open tasks must not overlap in `decomp/`.
- `flags`: `solo` (no review needed), `parallel-ok` (may start before the previous phase gate), `human` (needs a person with a cooked disc).
- `log`: dated one-liners. Append, don't rewrite.

Phase gates are in `docs/BUILD_SPEC.md` section 6.

---

## Phase 0: Spike

Gate: `--headless --frames 600` exits 0 on native and web; title screen
renders in Chromium; one human has seen it.

### P0-01 Repo init and scaffolding
- lane: shared
- status: open
- owner:
- blocked_by:
- touches: root, .gitignore, CMakeLists.txt, platform/*/CMakeLists.txt, server/, web/, tools/
- flags: solo
- what: Create the directory layout in README. Root CMake that includes per-directory CMakeLists. `.gitignore` with every game-data extension from AGENTS.md section 2 plus `game/`, `build/`, `node_modules/`. Empty vitest and doctest setups that run and pass. GitHub Actions workflow that runs both on PR. `THIRD_PARTY_NOTICES.md`.
- done when: CI is green on an empty repo.
- log:

### P0-02 Vendor decomp from GalaxyQuest
- lane: B
- status: open
- owner:
- blocked_by: P0-01
- touches: decomp/, docs/DECISIONS.md
- flags:
- what: Copy GalaxyQuest's `decomp/` at a pinned commit into `decomp/`. Tag the commit `petari-base`. Fill in D-001 with the hash. Confirm `git diff petari-base -- decomp` is empty. Write `decomp/README.md` saying what this is and that Petari's own build tooling is unused.
- done when: `decomp/` is in, tagged, documented.
- log:

### P0-03 Vendor GalaxyQuest platform layer, strip OpenXR
- lane: A
- status: open
- owner:
- blocked_by: P0-02
- touches: platform/gx, platform/audio, platform/os, platform/fs, platform/input
- flags:
- what: Copy GalaxyQuest `platform/` into our per-subsystem layout. Delete `xr/`. Replace every OpenXR and Android call with a TODO stub that compiles. Record the source commit in DECISIONS. Keep file-level attribution.
- done when: `platform/` compiles as a static library in the native target with stubs.
- log:

### P0-04 Native desktop backend (SDL2 + OpenGL)
- lane: A
- status: open
- owner:
- blocked_by: P0-03
- touches: platform/native, CMakeLists.txt, tools/bench/stub-game
- flags:
- what: SDL2 window, GL 3.3 core context (or GLES3 via ANGLE on macOS), keyboard input into the WPAD surface, `--game <dir>`, `--galaxy <name>`, `--headless --frames N`. Create `tools/bench/stub-game/` with the cooked layout but empty files so headless boot can be tested without a disc.
- done when: `./smg --game tools/bench/stub-game --headless --frames 60` exits 0 (boot + early-out is fine). Native build boots to title with a real cooked disc (human check, log it).
- log:

### P0-05 Cook tool: desktop path
- lane: B
- status: open
- owner:
- blocked_by: P0-02
- touches: tools/cook
- flags: human
- what: Copy GalaxyQuest `tools/cook/`. Make sure `cook.py extracted game` works on EUR. Document region handling in `tools/cook/README.md`. Add `--verify` that checks a cooked folder's manifest.
- done when: a human cooks their disc and the manifest verifies.
- log:

### P0-06 Emscripten build: compile and link
- lane: A
- status: open
- owner:
- blocked_by: P0-04
- touches: CMakeLists.txt, platform/web, web/
- flags:
- what: `-DSMG_TARGET=web`. Emscripten toolchain file. Flags: `-pthread`, `-sPROXY_TO_PTHREAD`, `-sOFFSCREENCANVAS_SUPPORT`, `-sINITIAL_MEMORY=268435456`, `-sALLOW_MEMORY_GROWTH`, `-sUSE_WEBGL2`, `-sFULL_ES3`. Read SMGwebport's `shell.js` for the flags they needed. A minimal `web/index.html` + Vite config with COOP/COEP headers that loads the wasm and shows console output.
- done when: the wasm links and the page loads it without a crash before `main()`.
- log:

### P0-07 OPFS disc reader (minimal)
- lane: B
- status: open
- owner:
- blocked_by: P0-03, P0-06
- touches: platform/fs, web/src/fs
- flags:
- what: DVD layer backed by `FileSystemSyncAccessHandle` on the game worker. Main-thread helper that copies a user-picked folder into OPFS `/smg/game/` (no cooking yet; assume pre-cooked on desktop). Handle cache. Manifest check on open.
- done when: the game worker can open and read every file in the manifest from OPFS.
- log:

### P0-08 Web boot to title screen
- lane: A
- status: open
- owner:
- blocked_by: P0-06, P0-07
- touches: platform/gx, platform/web, web/
- flags: human
- what: Get the GX layer rendering through WebGL2 on the OffscreenCanvas. Fix whatever breaks between GLES3 and WebGL2 (readbacks, texture formats, sync objects). Keyboard input only. Audio off.
- done when: title screen renders in Chromium from an OPFS-loaded cooked disc. Screenshot in the PR. Human confirms.
- log:

### P0-09 Phase 0 gate review
- lane: shared
- status: open
- owner:
- blocked_by: P0-04, P0-08
- touches: docs/DECISIONS.md, TASKS.md
- flags: human
- what: Run the gate. Decide D-005 (pthread + offscreen vs main thread) with evidence. Write a `log` summary of what's flaky. Open any P1 tasks that Phase 0 revealed.
- done when: gate passes and D-005 is final.
- log:

---

## Phase 1: Single-player in the browser

Gate: a human plays new game to the first star of Good Egg Galaxy in
Chromium, with sound, on a gamepad, at 60 fps on the reference machine.

### P1-01 Audio: DSP mixer to AudioWorklet
- lane: A
- status: open
- owner:
- blocked_by: P0-09
- touches: platform/audio, web/src/audio
- flags: parallel-ok
- what: Mixer output into a SAB ring (48 kHz stereo f32). AudioWorklet processor drains it. Resample if context rate differs. Start on first user gesture. Survive scene changes. Underrun counter exposed to the overlay.
- done when: title music and SFX play without crackle for 10 min.
- log:

### P1-02 In-browser cooker (Pyodide)
- lane: B
- status: open
- owner:
- blocked_by: P0-05, P0-07
- touches: web/src/import, tools/cook
- flags: parallel-ok
- what: Run `cook.py` under Pyodide in a worker. Input: a picked ISO/RVZ/WBFS or an extracted folder (File System Access API). RVZ needs decompression; if Pyodide can't, require extracted or ISO for Phase 1 and say so on screen. Output straight to OPFS. Progress events to the UI. `navigator.storage.persist()` before writing. Movies off by default.
- done when: an EUR ISO imports in under 10 min on the reference machine and the manifest verifies.
- log:

### P1-03 Import screen UI
- lane: A
- status: open
- owner:
- blocked_by: P1-02
- touches: web/src/screens/import
- flags:
- what: Pick file or folder, show requirements, run cooker, progress bar, errors in plain words, "movies on/off" toggle, storage quota display. Re-import and delete options in settings.
- done when: a non-technical person can import without reading docs.
- log:

### P1-04 Gamepad and mouse input
- lane: B
- status: open
- owner:
- blocked_by: P0-09
- touches: platform/input, web/src/input
- flags: parallel-ok
- what: Gamepad API polling on the main thread into a SAB ring. Mapping per BUILD_SPEC section 7. Virtual cursor from right stick with configurable speed. Mouse pointer when the canvas is focused. Keyboard fallback stays. Remap UI deferred to P1-09.
- done when: full playthrough of the prologue on a gamepad with pointer sections working.
- log:

### P1-05 Saves to IndexedDB
- lane: B
- status: open
- owner:
- blocked_by: P0-09
- touches: platform/fs, web/src/save
- flags: parallel-ok
- what: NAND surface backed by IndexedDB via an async bridge the game polls. Export/import save file as a download/upload in settings. Three save slots like the original.
- done when: save, reload page, continue.
- log:

### P1-06 Shader variant cache and seed
- lane: A
- status: open
- owner:
- blocked_by: P0-09
- touches: platform/gx, web/src/shaders, tools/bench
- flags: parallel-ok
- what: Key TEV configs by hash. Serialise generated GLSL. Persist to OPFS. Ship a seed file built by a `tools/bench/shader-tour.ts` script that visits every galaxy headless (human runs it once with a real disc; the seed contains no game data, only our GLSL).
- done when: second load of Good Egg has no shader hitch over 50 ms.
- log:

### P1-07 Frame pacing and resolution scale
- lane: A
- status: open
- owner:
- blocked_by: P0-09
- touches: platform/gx, platform/os, web/src/settings
- flags: parallel-ok
- what: Lock to 60 fps with rAF on the worker. Internal render scale setting (0.5x to 2x). Frame time overlay (p50/p95). Vsync drift handling.
- done when: 60 fps p95 under 16 ms in Good Egg at 1x on the reference machine.
- log:

### P1-08 Bench and CI baseline
- lane: A
- status: open
- owner:
- blocked_by: P1-07
- touches: tools/bench, .github/workflows
- flags:
- what: `tools/bench/run.ts` runs headless web build in Playwright-Chromium on the stub folder for 600 frames, captures frame time, compares to `baseline.json`, fails over +10%. Document how to update the baseline.
- done when: CI runs it on every PR.
- log:

### P1-09 Settings screen
- lane: A
- status: open
- owner:
- blocked_by: P1-04, P1-05, P1-07
- touches: web/src/screens/settings
- flags:
- what: Controls remap, cursor speed, render scale, volume, save export/import, re-import disc, delete data. Persist in localStorage.
- done when: every setting works and persists.
- log:

### P1-10 Playtest: prologue to first star
- lane: shared
- status: open
- owner:
- blocked_by: P1-01, P1-03, P1-04, P1-05, P1-06, P1-07
- touches: docs/PLAYTEST.md
- flags: human
- what: Write `docs/PLAYTEST.md` checklist for the Phase 1 gate. Run it. File a task for each crash or blocker found (P1-11+).
- done when: checklist passes end to end.
- log:

---

## Phase 2: Ghost co-op

Gate: 3 people in a room see each other move in Good Egg over the internet
with no desync complaints at 100 ms delay.

### P2-01 PlayerState encoder/decoder
- lane: B
- status: open
- owner:
- blocked_by: P0-09
- touches: net/state
- flags: parallel-ok
- what: Encode/decode per NETCODE.md. f16 helpers. Unit tests with round-trip and fuzz. No game dependency; pure C++.
- done when: tests pass.
- log:

### P2-02 Relay server (local Node)
- lane: B
- status: open
- owner:
- blocked_by:
- touches: server/
- flags: parallel-ok
- what: WebSocket server. Rooms by code. Hello/Welcome/Joined/Left. Relay PlayerState. Ping/Pong. Rate limit. Vitest with 5 fake clients. `npm run dev`.
- done when: soak test (5 clients, 30 Hz, 10 min) drops nothing.
- log:

### P2-03 NetClient and SAB ring
- lane: B
- status: open
- owner:
- blocked_by: P2-01, P2-02, P1-10
- touches: net/client, web/src/net
- flags:
- what: Main-thread WebSocket writes incoming frames to a SAB ring the game worker reads; game worker writes outgoing to another ring the main thread flushes. `NetClient` in C++ samples local player each 2nd frame and publishes. Per-remote ring of 32 states. Net status (RTT, loss) to overlay.
- done when: two tabs on one machine exchange states, visible in the overlay.
- log:

### P2-04 Local player state capture hook
- lane: A
- status: open
- owner:
- blocked_by: P1-10
- touches: decomp/src/Game/Player (one hook file, name it in the log before editing), net/capture
- flags:
- what: One `TARGET_WEB` hook after the player's update that copies position, rotation, up, velocity, current anim id and frame, powerup, grounded, spinning, galaxy and scenario into a plain struct `net/` reads. Find the right accessors; grep, don't guess. Document them in `net/capture/README.md`.
- done when: the struct updates every frame with correct values in Good Egg (log a dump).
- log:

### P2-05 RemotePlayerPuppet actor
- lane: A
- status: open
- owner:
- blocked_by: P2-03, P2-04
- touches: net/puppet, decomp/src/Game/Scene (spawn hook only; name the file in the log)
- flags:
- what: `LiveActor` subclass with Mario and Luigi model loading (copy an NPC's pattern), animation player, Hermite interpolation at now-100ms, freeze/hide rules, tint by palette, galaxy/scenario gating. Spawn on scene enter for every known remote; despawn on scene exit. Heap budget measured and logged.
- done when: a second tab's Mario is visible and moves smoothly in Good Egg.
- log:

### P2-06 Lobby screen
- lane: A
- status: open
- owner:
- blocked_by: P2-02
- touches: web/src/screens/lobby
- flags: parallel-ok
- what: Name, character, tint, create room (shows code), join by code, player list, "play solo". Relay URL setting (default localhost, Phase 3 sets the deployed one).
- done when: 3 people can get into a room without help.
- log:

### P2-07 Name tags and player list overlay
- lane: A
- status: open
- owner:
- blocked_by: P2-05, P2-06
- touches: web/src/overlay, net/bridge
- flags:
- what: Game worker posts projected screen positions for visible puppets each frame (or every other). Overlay draws name tags. Player list shows galaxy and status per player.
- done when: tags track puppets with no visible lag.
- log:

### P2-08 Playtest: 3 players over the internet
- lane: shared
- status: open
- owner:
- blocked_by: P2-05, P2-06, P2-07
- touches: docs/PLAYTEST.md
- flags: human
- what: Relay exposed via a tunnel (ngrok/cloudflared). 3 players, 30 min, Good Egg and Observatory. Record RTT, visible snapping, crashes. File tasks.
- done when: gate passes.
- log:

---

## Phase 3: Shared progress

Gate: 5 players, 2 hours, no one loses a star, late joiner sees the right
observatory state.

### P3-01 Progress log on the server
- lane: B
- status: open
- owner:
- blocked_by: P2-02
- touches: server/
- flags: parallel-ok
- what: Append-only log per room. Dedup by (origin, kind, galaxy, scenario, arg). ProgressLog send on join and on append. Ack tracking and resend. Tests.
- done when: tests cover join-in-progress and resend.
- log:

### P3-02 Star-get event capture and merge
- lane: A
- status: open
- owner:
- blocked_by: P2-08, P3-01
- touches: decomp/src/Game/System (save data hooks; name files in log), net/progress
- flags:
- what: Hook the star-get commit path to emit StarGet. On incoming StarGet not in local save, set the bit. Toast via bridge. Never clear. Verify the save writes.
- done when: two clients, one gets a star, the other sees it in their save after reload.
- log:

### P3-03 Story flags, unlocks, Observatory state
- lane: A
- status: open
- owner:
- blocked_by: P3-02
- touches: decomp/src/Game/System, net/progress
- flags:
- what: Same for StoryFlag, GalaxyUnlock, DomeUnlock, GrandStar. Find which flags the Observatory reads to decide layout. smg-online only shares "from the Observatory on"; decide and log whether the prologue is shared.
- done when: a late joiner's Observatory matches the host's.
- log:

### P3-04 Follow-host warp
- lane: B
- status: open
- owner:
- blocked_by: P3-03
- touches: net/warp, web/src/overlay, decomp/src/Game/Scene (scene change hook; name file in log)
- flags:
- what: Overlay button: go to the host's galaxy and scenario if it's unlocked locally. Uses the game's own scene change path.
- done when: pressing it lands you next to the host.
- log:

### P3-05 Deploy relay to Cloudflare
- lane: B
- status: open
- owner:
- blocked_by: P3-01
- touches: server/
- flags:
- what: Durable Object per room. Wrangler config. Env for allowed origins. Keep the Node path working for local dev. Document deploy in `server/README.md`.
- done when: a room works on the deployed URL.
- log:

### P3-06 Playtest: 5 players, 2 hours
- lane: shared
- status: open
- owner:
- blocked_by: P3-03, P3-04, P3-05
- touches: docs/PLAYTEST.md
- flags: human
- what: Run the gate. Record star loss, desync, crashes, net stats. File Phase 4 tasks ordered by what hurt most.
- done when: gate passes.
- log:

---

## Phase 4: Selective world sync (open-ended, host-authoritative)

Each object type is its own task. Add tasks here after P3-06, ordered by
playtest pain. Template:

### P4-NN Sync: <object type>
- lane:
- status: open
- owner:
- blocked_by: P3-06
- touches: net/objects, decomp/src/Game/<area> (name files in log), server/
- flags:
- what: Object identity scheme (galaxy, zone, index). Event schema (kind 100+). Host emits on state change. Clients apply. Late joiner gets current state from log. Test.
- done when: 5 players see the same state for this object type.
- log:

Starter list, unordered: switches, launch stars (fired), coins (collected), 1-up mushrooms, star bits (shared pool?), boss HP and phase, breakable crates, Pull Star activation.

---

## Phase 5: Polish (optional)

### P5-01 Rust/WASM cooker
- lane: B
- status: open
- owner:
- blocked_by: P1-10
- touches: tools/cook-rs, web/src/import
- flags: parallel-ok
- what: Only if P1-02 fails the 10 min budget. Port the hot path.
- log:

### P5-02 Safari support
- lane: A
- status: open
- owner:
- blocked_by: P3-06
- touches: platform/web, web/
- flags:
- what: OffscreenCanvas + WebGL2 + OPFS sync handle checks. Fallbacks. Test matrix.
- log:

### P5-03 Aurora/WebGPU renderer spike
- lane: A
- status: open
- owner:
- blocked_by: P3-06
- touches: platform/gx-webgpu (new)
- flags:
- what: Time-boxed 2 weeks. Bench against WebGL2. Decide in DECISIONS.
- log:

### P5-04 Mobile touch controls
- lane: A
- status: open
- owner:
- blocked_by: P5-02
- touches: web/src/input
- flags:
- what: Virtual stick and buttons. Only if the bench says mobile hits 30 fps.
- log:

### P5-05 Local 2-player (two real players, one machine)
- lane: shared
- status: open
- owner:
- blocked_by: P3-06
- touches: decomp/ (many), platform/
- flags:
- what: This is the hard one. Scope doc first, then tasks. Second `MarioActor`, camera, HUD, cutscenes all need work. Compare against Brocoli's 2021 SMG multiplayer dol patch for what broke.
- log:

---

## Blocked / parked

(move tasks here with a `blocked_on:` line when they can't proceed)

## Done

(reviewer moves tasks here on merge, newest first)
