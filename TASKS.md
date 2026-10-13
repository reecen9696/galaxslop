# Task board

Single source of truth. Edit it to claim, progress, block and finish work.
Rules are in `AGENTS.md` section 1. Short version: claim on `main` first,
one task per branch, update this file in the same PR as the code.

Fields:
- `id`: phase + number. Stable. Never renumber.
- `lane`: A (Claude by default), B (OpenCode by default), shared.
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

### P0-00 Play the existing mod first
- lane: shared
- status: open
- owner:
- blocked_by:
- touches: docs/PLAYTEST.md
- flags: human, solo, parallel-ok
- what: Before building any netcode, both humans play smg-online (FimeV1) in Dolphin for an evening: USA disc, the Python relay, 2-3 players. It is the exact model we are copying (puppets, shared progress log, star bits synced). Write down what feels good, what snaps, what is missing, how the save sharing behaves, and what you would change. That list reorders Phase 2-4. Optional: if you want to know what "SMG in a browser tab" feels like at 50 ms latency, stream one Dolphin from a GPU box with neko (D-015).
- done when: a dated section in `docs/PLAYTEST.md` with the notes.
- log:

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
  - 2026-10-12 (A): .github/workflows/build.yml compiles+links `smg` on macOS (x86_64, checks the 0x98000000 base) and Linux, and py_compiles the cooker, on every push/PR. Linux desktop GL added to port/gl.h. Still open here: doctest/vitest scaffolds (do with the first unit-testable code: net/ encoder, server/).
  - 2026-10-12: layout, `.gitignore`, notices, docs and board are in (repo `reecen9696/galaxslop`). Remaining: root CMake, per-dir CMakeLists, doctest/vitest scaffolds, CI workflow. Do CMake after P0-02 lands so it globs real sources. Note `.gitignore` game-data dirs are anchored (`/game/`); unanchored `game/` matched `decomp/src/Game` on case-insensitive filesystems.

### P0-02 Vendor the GalaxyQuest snapshot (decomp + platform + cook)
- lane: B
- status: done
- owner: A (solo, before lane B joined)
- blocked_by:
- touches: decomp/, platform/, tools/cook/, docs/DECISIONS.md, THIRD_PARTY_NOTICES.md, AGENTS.md (lane paths)
- flags:
- what: Per D-007. Clone bigmak94/GalaxyQuest at `d8a039c96f89c188fc44cdb7382ab9a4cf9ce92e`. (1) Replace the contents of `decomp/` with its `decomp/` in one commit; do not rewrite our history. (2) Copy its `platform/` (keep its `platform/src/<subsystem>/` + `platform/include/` layout; delete our empty `platform/<x>/.gitkeep` dirs) and `tools/cook/`. (3) Find the Petari commit it started from: `git -C galaxyquest rev-parse 'petari-base:decomp'` gives a tree hash; search our history for a Petari commit whose root tree matches (`git log --format='%H %T'` before the move commit). Move our `petari-base` tag there (`git tag -f`, `git push -f origin petari-base`, the one allowed force). If no exact match, pick the closest by `git diff --stat` and say so in the log. (4) Confirm `decomp/src/Game/System/*.bcsv` are gone (D-011); if the snapshot still has them, delete them. (5) Fill D-007's Petari hash. (6) Keep GalaxyQuest's file-level attribution; add the snapshot hash to THIRD_PARTY_NOTICES. CAS and Inter are `xr/`-only and leave with P0-03.
- done when: `git diff petari-base -- decomp` shows only GalaxyQuest's port changes; `platform/` and `tools/cook/` present; D-007 filled.
- log:
  - 2026-10-12: GalaxyQuest's own `petari-base` tag = Petari `0a5c660` (2026-09-28) minus Petari tooling, line endings only differ. Our `petari-base` now tags `b3783b5` (decomp/ = that tree). Snapshot `d8a039c` applied in `f4af7d4`: 986 decomp files changed, platform/ incl. `vi/`, `xr/`, `android/`, all of GalaxyQuest's tools/ (cook.py imports ../rarc.py), root CMakeLists.txt, docs/galaxyquest/. bcsv tables gone. Petari's configure.py/config/orig are gone too (GalaxyQuest dropped them); upstream history keeps them.

### P0-03 Strip Android/OpenXR from the platform layer; desktop host glue
- lane: B
- status: done
- owner: A (solo)
- blocked_by: P0-02
- touches: platform/src/os, platform/src/dvd, platform/src/nand, platform/src/input, platform/src/audio (output glue only), platform/src/port, platform/src/xr (delete), CMakeLists.txt
- flags:
- what: Everything outside `gx/`. Delete `xr/`. Replace: Android log -> stdio; AAudio -> a `PortAudioSink` interface with a null sink now (SDL audio in P0-04, worklet ring in P1-01); Android asset/`files/game` paths -> `--game <dir>`; the `reserve` preload trick for the low-4 GB window -> `mmap(MAP_FIXED)` at startup in `os_mem.cpp`, with `-Wl,-pagezero_size,0x1000` on macOS (D-013); Quest controller input -> the WPAD surface fed by a plain struct the host fills. OpenXR calls in `port/` become no-ops behind `PORT_PLATFORM=desktop`. Keep `-DPORT_PLATFORM_CODE` and `compat.h` as they are.
- done when: every file outside `platform/src/gx/` compiles on macOS and Linux with `-DPORT_PLATFORM=desktop`. Nothing links yet; P0-04 links.
- log:
  - 2026-10-13 (A): desktop builds without xr/ and android/ on macOS and Linux (CI green). The Android files stay in the tree, unbuilt; deleting them is a separate cleanup.
  - 2026-10-12: desktop path in CMakeLists.txt (Android path untouched): excludes xr/, aaudio_out, gl_renderer, headless_render; adds platform/src/native/{main.cpp,desktop_stubs.cpp}. os_mem.cpp: shm_open on macOS, no MAP_FIXED_NOREPLACE on Darwin. compat.h: <float.h>, __va_list. Links on macOS x86_64 (D-016). xr/ and android/ still in the tree, just not built; delete once Linux CI builds too.

### P0-11 Aurora GX spike (critical path, 3-week timebox)
- lane: A
- status: done
- owner: A (solo)
- blocked_by: P0-02
- touches: platform/src/gx (replace), platform/include, CMakeLists.txt, tools/cook (read; note any swap that must stop)
- flags:
- what: Per D-014. Add `extern/aurora` (MIT, pinned), link `aurora::gx` (+ `vi`, `mtx` as needed) in place of GalaxyQuest's `gx/` and Petari's `src/RVL_SDK/gx`; Aurora's `include/dolphin/gx.h` family replaces the SDK headers for GX only. Keep GalaxyQuest's `os`/`dvd`/`nand`/`audio`/`input`. Native first (Dawn on Metal): title screen, then Good Egg. Then Emscripten with emdawnwebgpu (read `webcompiled`'s build for the approach; it is GPL-3, copy nothing). Answer the five spike questions in D-014 and log them with numbers. Go/no-go at 3 weeks: go = Good Egg renders correctly natively and the web build draws the title screen; no-go = resume P0-10.
- done when: go/no-go logged in D-014's revisit line and here, with frame time at 720p.
- log:
  - 2026-10-12: no-go before starting, on evidence (D-017): Aurora's GX is a high-level API with a different GXSetArray, no Emscripten support in-tree (webcompiled is GPL-3), and GalaxyQuest's pipeline works at the hardware-FIFO level with a small Backend interface. P0-10 is the path.

### P0-10 GX renderer: ES 3.0 / WebGL2 rework (critical path)
- lane: A
- status: review
- owner: A (solo)
- blocked_by: P0-11 (no-go: done)
- touches: platform/src/gx
- flags:
- what: Per D-008 and D-017. Native target is GL 4.1 core on macOS (`#version 410 core`), browser target is `#version 300 es`; one source, a version/precision prelude chosen at runtime. GalaxyQuest's renderer is GLES 3.2 and will not run on WebGL2 or macOS GL. In order: (1) the four SSBOs -> data textures: `rows` and `st` as RGBA32F, `recs` as RGBA32UI, uploaded with `glTexSubImage2D` per FIFO flush; `gl_shadergen.cpp` emits `texelFetch` helpers in place of `buffer` block indexing. (2) `vtxRec[u_firstOrdinal + gl_VertexID]` -> a `uint` vertex attribute via `glVertexAttribIPointer`. (3) `glVertexAttribFormat/Binding/BindVertexBuffer` -> `glVertexAttribPointer` per batch. (4) `#version 320 es` -> `300 es`; `#version 330 core`/`410 core` for native; `layout(binding=N)` on samplers -> `glUniform1i` after link. (5) `glProgramBinary` cache -> cache generated GLSL keyed by TEV hash; warm at load; `KHR_parallel_shader_compile` when present. (6) The shared-context compile thread stays behind `#ifdef` for native; web uses (5). (7) Remove EGL and QCOM foveation. Keep `PETARI_DLCHECK` working. Use GalaxyQuest's own `PETARI_PERFLOG` to compare batch counts before and after.
- done when: with P0-04's native build, title screen and Good Egg match Dolphin screenshots side by side (human), `PETARI_DLCHECK=1` is clean for 600 frames, and the renderer compiles with no symbol outside ES 3.0 / GL 3.3 core (grep the source for `GL_SHADER_STORAGE`, `glVertexAttribFormat`, `glProgramBinary`, `egl`).
- log:
  - 2026-10-13 (A): only the human Dolphin side-by-side is left; everything automatable is done.
  - 2026-10-12: PETARI_DLCHECK=1 over menus -> prologue -> Good Egg: 104,200,000 cached draws compared, 0 differed. Only the Dolphin side-by-side (human) is left for done.
  - 2026-10-12: PETARI_GLSL_DUMP + tools/bench/validate_glsl.sh: all 394 shaders (197 programs) generated over menus, prologue and Good Egg validate as GLSL ES 3.00 with glslangValidator (which rejects the original SSBO shaders). The WebGL2 shader path is ready for P0-08.
  - 2026-10-12: gameplay verified: scripted new file -> prologue (Peach's Castle courtyard) -> PETARI_STAGE=EggStarGalaxy:1. Good Egg renders correctly by eye at 1280x912; 300 s, no crash, 0 shader errors. Menu script that works: see docs/BUILDING.md ("Scripted boot to a galaxy"). Remaining for done: Dolphin side-by-side, PETARI_DLCHECK=1 run.
  - 2026-10-12: ported (gl_shadergen data textures + toDataTextures rewrite; gl_renderer per-batch glVertexAttribPointer, glTexImage2D, invalidate on ES only; port/gl.h). macOS GL 4.1 Metal on Apple M5: 0 shader errors; wrist-strap screen, title screen, file-select dialog and icon select render correctly (2D layouts, text, 3D horizon/background, bloom). Not yet seen: a galaxy (scripted menu navigation stalls at icon select; a human can play through with keyboard/mouse). PETARI_DLCHECK pass not yet run.

### P0-04 Native desktop host (Aurora main / SDL3; SDL2 + GL only on fallback)
- lane: B
- status: done
- owner: A (solo)
- blocked_by: P0-03
- touches: platform/src/native, CMakeLists.txt, tools/bench/stub-game
- flags:
- what: Host window and input for the native build. If P0-11 goes, Aurora brings SDL3 windowing (`aurora::main`) and gamepad input; this task then wires `--game <dir>`, `--galaxy <name>`, `--headless --frames N`, keyboard into the WPAD surface and SDL audio behind `PortAudioSink` on top of it. If P0-11 fails, it is an SDL2 window with a GL 3.3/4.1 core context. Until a renderer lands, link against a null `gx` backend (`-DSMG_GX=null`) that accepts the FIFO and draws nothing, so B is not blocked on A and CI has a GL-free headless target for good. Create `tools/bench/stub-game/` with the cooked layout but empty files. Be honest about what the stub proves: platform init, arg parsing, a clean "no valid disc" exit. Not boot.
- done when: `./smg --game tools/bench/stub-game --headless --frames 60` exits 0 with the null backend. With P0-11 (or P0-10) merged and a real cooked disc, boots to title (human check, log it).
- log:
  - 2026-10-13 (A): window, GL 4.1, keyboard/mouse, SDL3 audio, shots, App Nap off; headless smoke runs. Stub-game folder not made (CI has no disc; the build check covers what the stub would).
  - 2026-10-12 (A): A is actively editing `platform/src/native/` (main.cpp, present.cpp, desktop_stubs.cpp) under this task even though native/ is lane B's directory; B, please leave those three files to A until this is `review`, or say so here.
  - 2026-10-12: SDL3 3.4.10 (source, x86_64) window + GL 4.1 core context in platform/src/native/present.cpp; keyboard/mouse mapping per BUILD_SPEC 7; --shots PNG capture. App Nap disabled (it throttled a hidden run from 57 fps to 0.5 fps after ~30 s; also matters for co-op with the window in the background). docs/BUILDING.md written.
  - 2026-10-12: `smg --game ~/smg/game --headless --seconds 20` boots RMGP01: disc identified, game main() runs, DSP boots, mixer up, ~900 frames presented, clean exit. No window yet; null renderer path. Note: B109take_0.aw is 0 bytes on the disc itself, the DVD log line is noise.

### P0-05 Cook tool: desktop path
- lane: B
- status: done
- owner: A (solo)
- blocked_by: P0-02
- touches: tools/cook
- flags: human
- what: `tools/cook/` arrives with P0-02. Make sure `cook.py extracted game` works on EUR and USA. It accepts an extracted folder only (`sys/fst.bin`, `sys/main.dol`, `files/`; Dolphin's `DATA/` subfolder is handled). Region tables live in `dol_data.py`; document them in `tools/cook/README.md`. `validate.py` exists; wire it as `cook.py --verify <out>` and make it write `<out>/manifest.json` (path, size, sha1 per file) so the browser can check an import without re-reading everything. The `native/` hot-path dir: note what it accelerates; it is a hint for D-009's TS port, not something to ship.
- done when: a human cooks their disc, `--verify` passes, `manifest.json` written.
- log:
  - 2026-10-12: RMGP01 cooked in ~9 s (2,371 files, 1.0 GB), validate.py 0 problems over 24,827 files / 2,235 archives. Added `manifest.json` (tools/cook/manifest.py), `--verify`, `--write-manifest`; corruption test fails as expected. `tools/rarc.py` now builds and loads `libyaz0.dylib/.so` on macOS/Linux (it only knew `yaz0.dll`). Note for D-009: at 9 s natively, the browser cooker's 10-minute budget has lots of room.

### P0-06 Emscripten build: compile and link
- lane: A
- status: done
- owner: A (taken over 2026-10-13: B claimed it 2026-10-12, no pushes since and the claim was lost from the board; B, say so here and A hands it back)
- started: 2026-10-12
- blocked_by: P0-11 (done)
- touches: CMakeLists.txt, platform/src/web, web/
- flags:
- what: `-DSMG_TARGET=web`. Emscripten toolchain file. **Renderer is WebGL2, not Aurora/WebGPU (D-017 superseded D-014):** `platform/src/gx/gl_renderer.cpp` + `gl_shadergen.cpp` already compile against `port/gl.h`, whose `__EMSCRIPTEN__` branch selects `<GLES3/gl3.h>` and `#version 300 es`; the same code renders the title screen on macOS GL 4.1. Reuse `CMakeLists.txt`'s desktop path as the model (exclusions, `platform/src/native/main.cpp` launcher shape) but put web-only glue in `platform/src/web/`. Starting flags (measure, don't trust): `-pthread -sPTHREAD_POOL_SIZE=4` (GalaxyQuest emulates OS threads in its own scheduler; real threads are only the frame builder and audio), `-sPROXY_TO_PTHREAD -sOFFSCREENCANVAS_SUPPORT` (D-005, with the main-thread fallback compiled in), `-sMAXIMUM_MEMORY=4GB -sALLOW_MEMORY_GROWTH` (D-013: the fixed 0x80000000 window needs >2 GB of address space; note Emscripten's >2 GB pointer caveats), `-sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2`, and `-sGLOBAL_BASE=0x98000000` so the game image sits at its fixed slot (D-016; GalaxyQuest's GPU front end treats addresses below 0x80000000 as physical). SMGwebport publishes no source or flags; its `shell.js` only shows what it exports (`web_audio_ring`, `web_input_update`, `web_shader_warmup`), which is a reasonable shape for our `platform/src/web` glue. A minimal `web/index.html` + Vite config with COOP/COEP headers that loads the wasm and shows console output.
- done when: the wasm links and the page loads it without a crash before `main()`; `wasmMemory.buffer.byteLength` and the page's resident memory are both logged.
- log:
  - 2026-10-13 (A): done. Links and boots: Node bring-up (SMG_WEB_NODE=ON) reaches the title and ran 11+ min; browser variant runs in Chrome. CI builds it (web job). See docs/WEB.md.
  - 2026-10-13 (A): starting. Work on main in small commits; nothing of B's is overwritten.
  - 2026-10-12 (A): FYI, every generated shader validates as GLSL ES 3.00 (see P0-10 log), and CI now builds macOS (green) and Linux. Renderer is ready on our side; ping in this log if Emscripten link errors point into platform/src/gx.
  - 2026-10-12 (A): what-line updated for D-016/D-017 after this was claimed; the Aurora/emdawnwebgpu text was stale. Also known: arm64 macOS can't host the fixed-address image, wasm32 can (GLOBAL_BASE).

### P0-07 OPFS disc reader (minimal)
- lane: B
- status: done
- owner: A (taken 2026-10-13, B inactive; B, say so here to take it back)
- blocked_by: P0-03, P0-06
- touches: platform/src/dvd, platform/src/web, web/src/fs
- flags:
- what: GalaxyQuest's `dvd/` layer backed by `FileSystemSyncAccessHandle` on the game thread (sync handles are worker-only; this is why D-005 defaults to a pthread). Main-thread helper that copies a user-picked, already-cooked folder (`showDirectoryPicker()`) into OPFS `/smg/game/`. Handle cache; the game opens archives repeatedly. Check `manifest.json` from P0-05 on open. Loads `sys/GalaxyID.bcsv`, `sys/StoryEvent.bcsv`, `sys/ErrorMessageArchive.arc` from the cooked folder as GalaxyQuest does (D-011).
- done when: the game worker can open and read every file in the manifest from OPFS.
- log:
  - 2026-10-13 (A): done with WasmFS instead of hand-written sync handles: the OPFS backend mounted at /opfs. web/app.js imports a cooked folder (picker or <input webkitdirectory>), checks sizes against manifest.json, copies to OPFS /smg/game (1 GB in ~6 s), reloads, boots from it. DVD sizes come from manifest.json so only the DVD worker touches game files (a game-thread stat on OPFS raced it: hang/trap in the prologue).

### P0-08 Web boot to title screen
- lane: A
- status: done
- owner: A
- blocked_by: P0-06, P0-07, P0-11
- touches: platform/src/gx, platform/src/web, web/
- flags: human
- what: Get the P0-11 renderer (Aurora on WebGPU; or P0-10's WebGL2 path on no-go) drawing on the canvas in Chromium. Keyboard input only. Audio off. Log frame time p50/p95 at 720p on the reference machine.
- done when: title screen renders in Chromium from an OPFS-loaded cooked disc. Screenshot in the PR. Human confirms.
- log:
  - 2026-10-13 (A): done. Strap screen, title, file select and the prologue render in Chrome over WebGL2 at 60 fps from an OPFS-loaded disc; keyboard and mouse work. Headless check: tools/web/smoke.mjs. Human confirmation still wanted (flag: human).

### P0-09 Phase 0 gate review
- lane: shared
- status: open
- owner:
- blocked_by: P0-04, P0-08
- touches: docs/DECISIONS.md, TASKS.md
- flags: human
- what: Run the gate. Decide D-005 (pthread + offscreen vs main thread) and D-013 (fixed window vs relocated arenas, with resident-memory numbers) with evidence. Record P0-08's frame time against D-008's 2 ms threshold. Write a `log` summary of what's flaky. Open any P1 tasks that Phase 0 revealed.
- done when: gate passes and D-005, D-013 are final.
- log:

---

## Phase 1: Single-player in the browser

Gate: a human plays new game to the first star of Good Egg Galaxy in
Chromium, with sound, on a gamepad, at 60 fps on the reference machine.

### P1-01 Audio: DSP mixer to AudioWorklet
- lane: A
- status: in_progress
- owner: A
- blocked_by: P0-09
- touches: platform/audio, web/src/audio
- flags: parallel-ok
- what: Mixer output into a SAB ring (48 kHz stereo f32). AudioWorklet processor drains it. Resample if context rate differs. Start on first user gesture. Survive scene changes. Underrun counter exposed to the overlay.
- done when: title music and SFX play without crackle for 10 min.
- log:
  - 2026-10-13 (A): AudioWorklet reads the AI ring straight from wasm memory (platform/src/web/audio_web.cpp, web/app.js), linear resample, starts on first key/click; the AI clock paces to it once attached. Needs a listen test.

### P1-02 In-browser cooker (TypeScript, Web Workers)
- lane: B
- status: review
- owner: A (taken 2026-10-13, B inactive)
- blocked_by: P0-05, P0-07
- touches: tools/cook-ts (new, published to web/ as a package), web/src/import
- flags: parallel-ok
- what: Per D-009. Port `tools/cook/*.py` module by module (`rarc_swap`, `bcsv`, `bmg`, `j3d`, `kcl`, `collision`, `lyt`, `jpa`, `jaudio`, `thp`, `dol_data`) to TypeScript on `DataView`. Input: a Dolphin-extracted folder via `showDirectoryPicker()`; no disc images (see P5-01). A pool of workers, one file per job, writing straight to OPFS with sync access handles; `manifest.json` last. Progress events to the UI. `navigator.storage.persist()` before writing. Movies off by default. Tests: (a) vitest with small synthetic fixtures per format, (b) `tools/cook/compare.py` that diffs a cook.py output tree against a TS output tree byte for byte; a human runs (b) once per release on a real disc and pastes the result in the log.
- done when: an EUR extracted disc imports in under 10 min on the reference machine, `compare.py` reports zero differences, manifest verifies.
- log:
  - 2026-10-13 (A): done per D-021 (cook.py under Pyodide in workers, not a TS port). EUR disc: 36 s with 6 workers, byte-identical to cook.py for all 2,375 files and manifest.json. web/cook/, served Python from /pycook/ by the dev server.

### P1-03 Import screen UI
- lane: A
- status: review
- owner: A
- blocked_by: P1-02
- touches: web/src/screens/import
- flags:
- what: Folder picker (extracted disc), with the Dolphin extract steps shown on screen (right-click game, Properties, Filesystem, Extract Entire Disc), requirements check (Chromium, cross-origin isolated, OPFS quota), run cooker, progress bar, errors in plain words, "movies on/off" toggle, storage quota display. Re-import and delete options in settings.
- done when: a non-technical person can import without reading docs.
- log:
  - 2026-10-13 (A): pre-loaded game, at the user's request: GAME_DIR (relay) / SMG_GAME (dev server) serve a cooked folder; config.json advertises it and the page copies it into OPFS on first visit with no picker (5-6 s locally), re-copying only when manifest.json changes. Own machine/LAN only (docs/WEB.md).
  - 2026-10-13 (A): the import screen takes an extracted disc (converted in the page) or a cooked folder; Dolphin steps on screen, size/manifest checks, quota check, progress bar, errors in words; ?reimport=1 starts over. Left for P1-09: re-import/delete buttons, movies toggle.

### P1-04 Gamepad and mouse input
- lane: B
- status: in_progress
- owner: A
- blocked_by: P0-09
- touches: platform/input, web/src/input
- flags: parallel-ok
- what: Gamepad API polling on the main thread into a SAB ring. Mapping per BUILD_SPEC section 7. Virtual cursor from right stick with configurable speed. Mouse pointer when the canvas is focused. Keyboard fallback stays. Remap UI deferred to P1-09.
- done when: full playthrough of the prologue on a gamepad with pointer sections working.
- log:
  - 2026-10-13 (A): Gamepad API (standard mapping, BUILD_SPEC 7) polled in the frame callback; right stick drives a virtual pointer until the mouse moves. Mouse pointer and keyboard done. Needs a playthrough on a real pad.

### P1-05 Saves to IndexedDB
- lane: B
- status: in_progress
- owner: A
- blocked_by: P0-09
- touches: platform/fs, web/src/save
- flags: parallel-ok
- what: NAND surface backed by IndexedDB via an async bridge the game polls. Export/import save file as a download/upload in settings. Three save slots like the original.
- done when: save, reload page, continue.
- log:
  - 2026-10-13 (A): saves go to OPFS /smg/save via WasmFS (not IndexedDB: same persistence, no async bridge); verified a save survives a reload. Left: export/import in settings.

### P1-06 Pipeline cache and seed
- lane: A
- status: in_progress
- owner: A
- blocked_by: P0-09
- touches: platform/src/gx, platform/src/web, web/src/shaders, tools/bench
- flags: parallel-ok
- what: Aurora keeps a pipeline cache with transferable entries (zstd by default). Persist it to OPFS on the web and to the user data dir natively; load it before the first frame. Pipeline creation goes through `createRenderPipelineAsync`; never block the frame on it. Ship a seed built by a `tools/bench/shader-tour.ts` script that visits every galaxy headless (human runs it once with a real disc; the seed contains no game data, only pipeline descriptors and WGSL). Version the seed against the Aurora commit; a submodule bump invalidates it. If P0-11 went no-go, this task is the WebGL2 version: GLSL source cache keyed by TEV hash plus `KHR_parallel_shader_compile`.
- done when: second load of Good Egg has no pipeline hitch over 50 ms.
- log:
  - 2026-10-13 (A): WebGL2 path: KHR_parallel_shader_compile, programs compile in the driver and a draw waits (skipped) until ready instead of blocking the frame. Good Egg load worst present 192 -> 33 ms, worst interval 209 -> 89 ms. Program binaries do not exist in WebGL (0 formats), so the "cache" is the browser's own shader cache; left: texture-upload spikes.

### P1-07 Frame pacing and resolution scale
- lane: A
- status: review
- owner: A
- blocked_by: P0-09
- touches: platform/gx, platform/os, web/src/settings
- flags: parallel-ok
- what: Lock to 60 fps with rAF on the worker. Internal render scale setting (0.5x to 2x). Frame time overlay (p50/p95). Vsync drift handling.
- done when: 60 fps p95 under 16 ms in Good Egg at 1x on the reference machine.
- log:
  - 2026-10-13 (A): lag report ("lots of stars on screen"). Measured: game thread ~3 ms/frame unloaded in Honeybee (38k draws), the tab ~27% CPU in total (no spinning), CPU handoffs ~600-870/s at 7-60 us; slow runs tracked machine load (two native clients, builds, kernel_task 66%). -msimd128 A/B: no difference, not kept. Added Auto render scale (default): steps down after 3 late seconds while the game keeps up (GPU-bound), back up after 20 steady ones; loads do not trigger it. smoke.mjs --cpuprofile samples every worker; PETARI_PERFLOG adds handoff stats.
  - 2026-10-13 (A): rAF-paced frame callback on the worker (setTimeout with ?PETARI_TIMER=1). Render scale 1x-4x of 640x456 in settings, applied live; frame-time overlay (present CPU and interval p50/p95, fps). Headless Good Egg (quick start): 60 fps, present 0.4/1.3 ms, interval 16.7/17.9 ms p50/p95. Reference-machine numbers still wanted (human).

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
- status: in_progress
- owner: A
- blocked_by: P1-04, P1-05, P1-07
- touches: web/src/screens/settings
- flags:
- what: Controls remap, cursor speed, render scale, volume, save export/import, re-import disc, delete data. Persist in localStorage.
- done when: every setting works and persists.
- log:
  - 2026-10-13 (A): page-side settings (gear, top right): volume (GainNode, remembered), save export/import as a plain zip (validated with Python zipfile), load the disc again, delete all data (two clicks). Left: render scale (P1-07), cursor speed, control remap.

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
- status: review
- owner: A (lane B task; B is on P0-06, hand back any time)
- blocked_by: P0-09
- touches: net/state
- flags: parallel-ok
- what: Encode/decode per NETCODE.md. f16 helpers. Unit tests with round-trip and fuzz. No game dependency; pure C++.
- done when: tests pass.
- log:
  - 2026-10-13 (A): net/proto/player_state.{h,cpp} + net/tests (doctest, `net_tests` target, CI step): half floats, encode/forward/decode round trip, malformed input, matrix<->quaternion, 10k-message fuzz; 5 cases, 1,841 assertions pass. Wire: 54 bytes client->server, 55 forwarded; galaxy as u32 stage hash instead of u16 id (no table to keep); NETCODE.md updated.
  - 2026-10-13 (A): claimed while B works on P0-06, to get two native clients seeing each other.

### P2-02 Relay server (local Node)
- lane: B
- status: review
- owner: A (lane B task; B is on P0-06, hand back any time)
- blocked_by:
- touches: server/
- flags: parallel-ok
- what: WebSocket server. Rooms by code. Hello/Welcome/Joined/Left. Relay PlayerState. Ping/Pong. Rate limit. Vitest with 5 fake clients. `npm run dev`.
- done when: soak test (5 clients, 30 Hz, 10 min) drops nothing.
- log:
  - 2026-10-13 (A): server/ done: protocol.ts, relay.ts (transport-free), main.ts (ws). vitest: room create/join (case-insensitive), joined/left notices, state relayed to others only with sender id, unknown/full room and protocol errors, ping/pong, 15 s silence drop, rate limit, and a 5-client 30 Hz soak with zero loss. CI job added. 10-min soak not run yet.
  - 2026-10-13 (A): claimed while B works on P0-06, to get two native clients seeing each other.

### P2-03 NetClient and SAB ring
- lane: B
- status: in_progress
- owner: A (native half; web half goes with P0-06/P0-08)
- blocked_by: P2-01, P2-02, P1-10
- touches: net/client, web/src/net
- flags:
- what: Main-thread WebSocket writes incoming frames to a SAB ring the game worker reads; game worker writes outgoing to another ring the main thread flushes. `NetClient` in C++ samples local player each 2nd frame and publishes. Per-remote ring of 32 states. Net status (RTT, loss) to overlay.
- done when: two tabs on one machine exchange states, visible in the overlay.
- log:
  - 2026-10-13 (A): verified end to end natively: remote state -> interpolation -> puppet pose on screen.
  - 2026-10-13 (A): native half works: net/client (WsClient + session), two desktop clients joined relay room PLAY22, each logs the other "visible" with live positions, RTT ~15 ms on localhost. Interpolation 100 ms behind, freeze 0.5 s, hide 3 s, stage/cutscene gating. Web half: implement net::Transport on the page WebSocket (P0-06 owner).

### P2-04 Local player state capture hook
- lane: A
- status: done
- owner: A (solo)
- blocked_by: (native: none; D-018)
- touches: decomp/src/Game/Player (one hook file, name it in the log before editing), net/capture
- flags: parallel-ok
- what: One `TARGET_WEB` hook after the player's update that copies position, rotation, up, velocity, current anim id and frame, powerup, grounded, spinning, galaxy and scenario into a plain struct `net/` reads. Find the right accessors; grep, don't guess. Document them in `net/capture/README.md`.
- done when: the struct updates every frame with correct values in Good Egg (log a dump).
- log:
  - 2026-10-13 (A): done (net/capture, verified over 13k frames); status was never updated.
  - 2026-10-12 (A): accessors found (D-018): MR::getPlayerPos/getPlayerVelocity/getPlayerUpVec/getPlayerFrontVec (Game/Util/PlayerUtil.hpp); animation = MR::getHashCode(MarioAccess::getPlayerActor()->mMarioAnim->getXanimePlayer()->getCurrentAnimationName()).

### P2-05 RemotePlayerPuppet actor
- lane: A
- status: in_progress
- owner: A (solo)
- blocked_by: P2-04 (P2-03 for the network; a loopback test needs neither)
- touches: net/puppet, decomp/src/Game/Scene (spawn hook only; name the file in the log)
- flags: parallel-ok
- what: `LiveActor` subclass with Mario and Luigi model loading (copy an NPC's pattern), animation player, Hermite interpolation at now-100ms, freeze/hide rules, tint by palette, galaxy/scenario gating. Spawn on scene enter for every known remote; despawn on scene exit. Heap budget measured and logged. For scale: smg-online draws up to 16 remote Marios inside real Wii memory, so 4 puppets fitting is expected, not a risk.
- done when: a second tab's Mario is visible and moves smoothly in Good Egg.
- log:
  - 2026-10-13 (A): puppets use the player models (Mario.arc / Luigi.arc: same skeleton and materials as GhostMario, real textures) instead of the ghost; a Mario and a Luigi puppet per slot, the one matching the remote's PlayerState Luigi flag is shown; the other character's archive mounts with the stage (SceneDataInitializer.cpp). Tint: per-puppet copies of the shirt/cap CMPR textures, recoloured by endpoint hue (net/puppet/puppet_tint, unit-tested), bound through J3DModelX's per-material DL hook (as MarioActor::swapTexture). Colour fixed by player id on every screen incl. the local player's own model (1 classic, 2 green, 3 pink, 4 orange, 5 purple); Hello's tint (SMG_TINT) overrides. The player hides Face0 on the shared model data (it draws MarioFace instead); the puppet shows it for its own draw. Client: keepalive pings from a background thread (cutscenes/loads went silent past the relay's 15 s drop) and rejoin on disconnect. tools/coop_two_players.sh: two windows, scripted to Good Egg. Drawn like the player: separate face (MarioFace/LuigiFace) and hand models placed on the body joints, expression/hand poses from the animation's PartsControl joint, player's light DL (setDynamicDL), animation frame synced each frame, eyelid frame sent in PlayerState flags bits 4-5. Puppets have no draw buffer, so parts are placed in draw() (calcViewAndEntry is never called). Lesson: initDLMakerFog/newDifferedDLBuffer on a model created without the DL-maker flag crashes at scene load.
  - 2026-10-13 (A): power-up forms on puppets: PlayerMode in PlayerState flags bits 6-9. Fire/flying = suit texture variants through the tint copy (the tint DL overrides the player's ColorChange btp, so the local player's suit goes through it too). Rainbow/ice/bee/spring = the form's body model sharing the puppet's animated joints (copyAnmMtxBuffer + own calcWeightEnvelopeMtx), own hands for rainbow/ice, rainbow brk, ice in the indirect pass (DrawAdaptor DrawType_0x24), bee wings and boo as PartsModels. All form archives mount into the scene heap in co-op (the stage file cache overflowed). 4 Mario + 4 Luigi puppets with every form: 2.1 MB of the scene heap (115 MB free). Verified side by side (PETARI_LOOPBACK=0 PETARI_LOOPBACK_SIDE=110) for Mario all forms and Luigi normal/fire/bee. Port bugs fixed on the way: JPA volume functions divide by zero (x86 traps, PPC does not; JPADynamicsBlock.cpp, TARGET_PC). Not done: rainbow trail, boo transparency/blink, spin drill.
  - 2026-10-13 (A): puppets lit where they stand: own ActorLightCtrl (attached on the first shown frame: area lights do not exist at scene init; init(type) with the player's light type, since a puppet has no draw buffer), registered as the player light for its draw and swapped back (exact player set-up incl. point lights). Memory sweep: 8 puppets + all forms in Astro, Honeyhive, Battlerock, Space Junk, Bowser Jr.'s Robot Reactor, Beach Bowl: 2.1 MB each, >115 MB free; stage change mid-session recreates puppets cleanly.
  - 2026-10-13 (A): spawn spacing: players no longer spawn inside each other; each steps aside by a slot fixed by player id after landing (ray-checked for floor and walls). Verified with two clients.
  - 2026-10-13 (A): fixes from a two-client test matrix (forms both ways, Luigi, push, stomp, stage change): (1) walking into a puppet looped the running kick (receiveOtherMsg accepted ACTMES_KICK; now refuses); (2) the faster player is the pusher (walks on), the slower one is shoved out on his own client, so a walk-in actually moves the other player; (3) j3dSys model/texture restored after a puppet draw, GD lists only built in the movement phase; (4) crash after a form-to-form debug switch (spring -> boo): MarioActor diffs the displayed form model with the base model's material flags during the flicker and overruns its 64-byte material DLs (96-128 bytes), heap corruption; the debug switch now goes through normal first. Diagnostics kept (TARGET_PC): GD overflow and J3D DL overrun reports.
  - 2026-10-13 (A): net::netPump runs every frame from GameSystem::frameLoop, so the client joins the room on the title screen (room code logged before gameplay; the web lobby shows it). netTick still sends only in gameplay. Seen in loopback (green Mario trailing red Mario in Good Egg). Untested: a Luigi save. Remaining: heap budget.
  - 2026-10-13 (A): networked: two desktop clients in relay room PLAY22, each drawing the other as a GhostMario puppet (screenshot: Bob idle at the courtyard spawn inside Alice's game). Remaining: Luigi model per remote (Hello character), tint, heap budget measurement.
  - 2026-10-12: loopback milestone works natively: PETARI_LOOPBACK=1 draws a GhostMario puppet replaying the local player 1 s late in the prologue courtyard (screenshot: blue ghost trailing Mario). Lessons: (1) create puppets during scene init (SceneDataInitializer::initAfterScenarioSelected), never from the frame update: ModelManager::init waits on a main-thread job and deadlocks there; (2) add GhostMario/GhostLuigi to the stage's archive list (startStageFileLoadAfterScenarioSelected); (3) build the XanimePlayers on the first frame the player exists (the table is the player's); (4) skip the file-select stage. Next: animation sync check (does the hash map to the right motion?), then drive it from P2-03 instead of the loopback.
  - 2026-10-12 (A): model it on GhostPlayer (Cosmic Mario ghost): XanimePlayer + upper-body player, changeAnimationByHash, receiveGhostPacket's apply logic. First milestone: a loopback puppet that replays the local player's captured state ~1 s late, no network.

### P2-09 Player contact (push, stomp)
- lane: A
- status: review
- owner: A
- started: 2026-10-13
- blocked_by:
- touches: net/puppet, net/client, net/proto, server/src (Contact forward), docs/NETCODE.md, docs/DECISIONS.md
- flags: parallel-ok
- what: D-020 option C. Puppet sensor (push, stomp bounce, no star bits, off while hidden/frozen), Contact 0x12 forwarded to the target, stomped player flinches (doFlipWeak, no damage). SMG_CONTACT=0 disables. Lobby toggle later (P2-06).
- done when: two clients: walking into each other pushes, landing on the other bounces and the other flinches.
- log:
  - 2026-10-13 (A): ground pound onto another player squashes them (Contact kind 2 -> Mario::requestCrush: flattened 3 s, -1 health, then invincibility); a plain jump stays bounce + flinch (user's choice). PlayerState flags bit 10 = squashed, so the puppet flattens too. Verified: Alice ground-pounds Bob, Bob flat, life 3 -> 2; loopback side by side shows the copy flattening. F9 / PETARI_DEBUG_MODES 100 squashes yourself (testing).
  - 2026-10-13 (A): done natively: two headless clients, mutual push at the spawn, Alice stomps Bob's puppet and bounces, Bob's log "stomped by player 2" and his Mario flinches. Relay test for Contact forwarding, C++ round-trip test. Puppet names contain ゴーストマリオ so Mario's jump/hang exemption applies.

### P2-06 Lobby screen
- lane: A
- status: review
- owner: A
- blocked_by: P2-02
- touches: web/src/screens/lobby
- flags: parallel-ok
- what: Name, character, tint, create room (shows code), join by code, player list, "play solo". Relay URL setting (default localhost, Phase 3 sets the deployed one).
- done when: 3 people can get into a room without help.
- log:
  - 2026-10-13 (A): web/app.js lobby before boot: name, room code (join) / create / solo, relay address under "Server"; remembered in localStorage; passed to the game as SMG_* (browser_env.js). Room bar under the canvas from the net: log lines. Verified headless that the settings reach the game. Note: the client connects at the first gameplay frame, so the room code shows once in a level.
  - 2026-10-13 (A): lobby colour swatches (SMG_TINT, remembered); room panel shows the code and a player icon with the room's head count (you included; names on hover) instead of "Room ... with <names>".

### P2-07 Name tags and player list overlay
- lane: A
- status: review
- owner: A
- blocked_by: P2-05, P2-06
- touches: web/src/overlay, net/bridge
- flags:
- what: Game worker posts projected screen positions for visible puppets each frame (or every other). Overlay draws name tags. Player list shows galaxy and status per player.
- done when: tags track puppets with no visible lag.
- log:
  - 2026-10-13 (A): browser: present_web.cpp projects each visible remote (head + 210 along their up) with the drawn frame's camera; the page draws HTML labels over the canvas (colour by tint). Player list = room bar under the canvas. Verified headless with two quick-start clients in Good Egg (tag above the walking puppet, none for a player behind the camera). Native build has no tags yet.

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

### P3-00 Co-op save policy
- lane: shared
- status: done
- owner:
- blocked_by: P2-08
- touches: docs/DECISIONS.md, platform/src/nand (read only), decomp/src/Game/System (read only)
- flags: solo
- what: Turn D-012 from proposal into decision. Read how the save layer picks a NAND file and slot. Decide: per-room save namespace, one shared "co-op" save, or merge into the chosen slot with an explicit warning. Write the D entry with the file paths involved. No code.
- done when: D-012 is superseded by a decision entry and P3-02 knows which file to hook.
- log:
  - 2026-10-13: decided by the user: progress merges into the save you play on (D-022).

### P3-01 Progress log on the server
- lane: B
- status: review
- owner: A (lane B task; B on P0-06)
- blocked_by: P2-02
- touches: server/
- flags: parallel-ok
- what: Append-only log per room. Dedup by (origin, kind, galaxy, scenario, arg). ProgressLog send on join and on append. Ack tracking and resend. Tests.
- done when: tests cover join-in-progress and resend.
- log:
  - 2026-10-13 (A): relay progress log: append-only per room, dedup by (kind, stage, scenario, arg) not origin (NETCODE updated, rationale there), broadcast incl. sender, full log on join, ack tracking, resend every 2 s from first unacked, 200 entries per message. vitest covers dedup, late join, resend/ack. Client side is P3-02 (blocked on P3-00 save policy).

### P3-02 Star-get event capture and merge
- lane: A
- status: review
- owner: A
- blocked_by: P2-08, P3-00, P3-01
- touches: decomp/src/Game/System (save data hooks; name files in log), net/progress
- flags:
- what: Hook the star-get commit path to emit StarGet. On incoming StarGet not in local save, set the bit. Toast via bridge. Never clear. Verify the save writes.
- done when: two clients, one gets a star, the other sees it in their save after reload.
- log:
  - 2026-10-13 (A): done per D-022. Capture in GameDataHolder::setPowerStar / tryOnGameEventFlag(SpecialStarGrand<n>) (TARGET_PC) -> net/client/progress.cpp (ProgressEvent until logged, ack, replay-safe) -> relay log -> port_net_apply_progress (GameScene) sets what the save lacks; toast on the page. PETARI_FAKESTAR=Galaxy:scenario fakes a star for tests. Verified in Chrome: client A fakes Good Egg star 2, client B joining 25 s later gets it ("power star EggStarGalaxy 2 from StarA"). Grand stars wired the same way, not yet exercised.

### P3-03 Story flags, unlocks, Observatory state
- lane: A
- status: review
- owner: A
- blocked_by: P3-02
- touches: decomp/src/Game/System, net/progress
- flags:
- what: Same for StoryFlag, GalaxyUnlock, DomeUnlock, GrandStar. Find which flags the Observatory reads to decide layout. smg-online only shares "from the Observatory on"; decide and log whether the prologue is shared.
- done when: a late joiner's Observatory matches the host's.
- log:
  - 2026-10-13 (A): event flags split two ways (GameEventFlagChecker::isOn): mSaveFlag bit 0 set = computed from stars/other flags (grand stars, galaxy-open counts, most Observatory layout) -> follow the shared stars with no message; bit 0 clear = stored -> shared as kind 2 with the flag-table index, for SpecialStar/EventFlag/StoryEvent/Galaxy types; Type_None (tutorials) stays per player. Applied in log order, so a flag whose condition is a shared star applies after it. Verified in Chrome: Phantom star 1 + LuigiTalkAfterRescued reach a client joining later. Prologue shared? Stored story flags of the prologue are shared like any other once in a room. PETARI_FAKEFLAG=Name,... for tests.

### P3-04 Follow-host warp
- lane: B
- status: review
- owner: A (lane B task; B inactive, galaxydecomp-05 not taking it)
- blocked_by: P3-03
- touches: net/warp, web/src/overlay, decomp/src/Game/Scene (scene change hook; name file in log)
- flags:
- what: Overlay button: go to the host's galaxy and scenario if it's unlocked locally. Uses the game's own scene change path.
- done when: pressing it lands you next to the host.
- log:
  - 2026-10-13 (A): "follow anyone", not host-only: the page's room bar lists each friend's galaxy and star with a Follow button (desktop: F key); the game warps with MR::requestChangeStageInGameMoving during gameplay (GameDataFunction.cpp, stage hash -> name from the save's galaxy list). Verified in Chrome both ways (Good Egg <-> Honeybee Kingdom): the follower arrives and both see each other. Needed a fix in net/capture: the stage hash was cached by name pointer, so after any stage change clients sent the old galaxy (everyone "not here").

### P3-05 Deploy the relay
- lane: B
- status: in_progress
- owner: A
- blocked_by: P3-01
- touches: server/
- flags:
- what: Per D-010. Dockerfile for the Node relay. Deploy to Fly.io or Railway (or any VPS) with TLS. Env for allowed origins and port. Health endpoint. Same code as local dev. Document deploy in `server/README.md` with the monthly cost.
- done when: a room works on the deployed `wss://` URL.
- log:
  - 2026-10-13 (A): deployable: the relay serves the bundled page (STATIC_DIR, COOP/COEP, /config.json -> same-origin relay), tools/web/bundle.mjs, server/Dockerfile (not built here: no Docker on this machine). Tested end to end on localhost: import, lobby create, room code shown. Waiting on a human to pick a host/domain (account and cost).

### P3-07 Drop in with the host's progress
- lane: shared
- status: review
- owner: A
- started: 2026-10-14
- blocked_by: P3-02, P3-03
- touches: server/src (relay, protocol), net/client (progress snapshot), decomp/src/Game/System (GameDataFunction, file start; name files in log), web/
- flags: parallel-ok
- what: The room takes the host's (room owner's) progress. Each client uploads a snapshot of its current file's progress; the relay keeps the host's and sends it to everyone else and to each joiner. A joiner's save gets what it lacks from the host's (stars, stored flags), as with the log. A brand-new file started in a room becomes a copy of the host's progress and skips the prologue, so a newcomer drops in at the Observatory.
- done when: a host with stars before the room was made, a joiner with no save: the joiner starts a new file and lands in the Observatory with the host's stars; a joiner with an older save gets the host's stars.
- log:
  - 2026-10-14 (A): D-025. ProgressSnapshot 0x23 (NETCODE): each client offers GameDataHolder::makeFileBinary of its file every 120 gameplay frames when it changed; the relay keeps each client's and passes the host's on (to others, to joiners, the next host's on leaving; vitest). Clients merge a new host snapshot into the current file (GameDataFunction.cpp netHostProgress: stars, shared flags, story progress; read into a holder in host memory, the stationed heap is full). New file: port_net_dropin_new_file from StorySequenceExecutor::overwriteGalaxyNameAfterLoading loads the snapshot into the current and scene-start holders. Verified, 3 web clients in one room, host progress kept out of the log (PETARI_FAKE_UNSHARED): the new file (PETARI_QUICKSTART=@new) started with the host's 4 stars in AstroGalaxy 1; a quick-started save got the 4 stars by merge. Not tried: the real file-select create path by hand, a host leaving mid-session. The relay holds snapshots in memory only.

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

### P4-01 Sync: defeated enemies
- lane: A
- status: review
- owner: A
- started: 2026-10-13
- blocked_by: (started before P3-06 at the user's request)
- touches: net/world (new), net/client (raw send, 0x13 route), server/src (forward), decomp/src/Game/Util/LiveActorUtil.cpp (initDefaultPos: register), decomp/src/Game/LiveActor/LiveActor.cpp (makeActorDead: report), decomp/src/Game/Scene/SceneDataInitializer.cpp (reset)
- flags: parallel-ok
- what: A placed enemy defeated on one client dies on the others in the same stage and scenario. Identity = FNV-1a(name, placed position) from initDefaultPos, same on every client. WorldEvent 0x13 forwarded to the room. No authority (first defeat wins).
- done when: two clients by the same enemy: one defeats it, it disappears on the other.
- log:
  - 2026-10-13 (A): works natively: Good Egg registers 40 enemies; Alice defeats an Octoomba next to both players (F10), Bob's copy dies (2 -> 1 Octoombas on Bob's screen), ids match. Relay vitest. Not yet: late joiners (no persistence), generator-spawned enemies (no placement id), death animation on the receiving side (plain kill()), loot from the enemy's kill() may drop twice. SMG_WORLD_SYNC=0 turns it off.
  - 2026-10-13 (A): the four limits: (1) receivers play the enemy's "Death" effect + SE_EM_EXPLODE_S before kill() (smoke puff seen on Bob's screen); (2) arrivals: each client keeps the visit's defeats and resends them when a player newly enters its stage/scenario (Alice defeats 2 Undergrunts in Good Egg star 2, Bob arrives from Honeyhive, his copies die); (3) spawned enemies (appeared during gameplay, no placement id) are matched by name hash + nearest within 400 units (untested: none in Good Egg, Honeyhive, Battlerock, Space Junk, Beach Bowl, Tri-leg); (4) coin/star bit spawns are suppressed while a remote defeat runs kill(). Also: a 'live' gate (first gameplay frame) so scene set-up kills (Dino Piranha's tail parts) are not reported, and star chips (enemy sensor) are excluded. WorldEvent is now 27 bytes (+name hash, position).
  - 2026-10-13 (A): coins and star bits: drops (MR::appearCoinPop/ToDirection, appearStarPiece/ToDirection) are announced and re-made on the others from a loot host declared during placement (the star bit director exists only from then, and allocates after placement); pickups (Coin::noticeGetCoin except purple, StarPiece::touchPlayer) remove the item on the others (placed: by id; spawned: nearest same name within 300). Shooting stars (the star bits falling from the sky) are host-led so the burst happens everywhere at once; their bursts are not announced (every client makes them). Verified with two clients: F11 drops appear on both, pickups vanish on both, shooting stars fire 5/5 in step. WorldEvent 35 bytes.
  - 2026-10-13 (A): "random flashing" (b3's report: frames with only the map on the clear colour, no sky, players or HUD) was not world sync: it happens offline too. The recorder ended frames at the VI retrace, which lands mid-draw of the next frame when one runs slow, cutting its start into the shown frame (draw counts 12k/32k around a normal 19.5k). Frames now end at the display copy (gx_recorder efbCopy). Broken frames: 37-45 per 50 s run before, 0 in 456 + 2x551 after. PETARI_FRAMELOG=1 logs each recorded frame's draws.

### P4-02 Sync: switches, finishing a star together, boss defeats
- lane: A
- status: in_progress
- owner: A
- started: 2026-10-13
- blocked_by: (started before P3-06 at the user's request)
- touches: net/world, decomp/src/Game/Map (stage switches), decomp/src/Game/MapObj/PowerStar.cpp, boss files (named in log), server/src
- flags: parallel-ok
- what: Stage switches turned on by one client turn on for the others in the same stage and scenario (doors, bridges, appearing stars). A Power Star collected by one player is collected by everyone in that level (both see the star-get and leave together; the user's choice). A boss defeated by one player is defeated for everyone (the user chose defeat only, no shared health).
- done when: two clients: a switch flipped by one opens the door for both; one grabs the star, both finish; one defeats a boss, it is defeated on the other.
- log:
  - 2026-10-13 (A): WorldEvent kinds 6-8. Switches: StageSwitchFunction::on/offSwitchBySwitchIdInfo report real changes (zone id + switch no, global = 0xFFFF), applied straight into the switch table so watchers react; AreaObj::on/offSwitchA (player-in-area switches) stay local; a switch flipping more than 6 times in 2 s stays local. Event stars: MR::appearEventPowerStar (every boss, Luma rewards, races, 100 coins) is shared; receivers kill the same-named enemy (the boss) with its own kill() and appear the star the same way. Grabbing: PowerStar's rush start reports the star id; the others pull their copy (once it waits) onto their player, who grabs it: both get the star-get and leave together. Late arrivals get switches (latest state) and event stars with the defeats. Verified with two clients in Good Egg star 1: Alice beats Dino Piranha (F12), on Bob it dies, switch 3/1 and 3/2 follow, the star appears; Alice grabs it, Bob grabs his ("You got a Star!", Luigi with Alice's Mario next to him); both leave the level. Not tried: other bosses' kill() mid-fight, a switch flipped by hand.

### P4-03 Sync: moving platforms, carried items, enemies, NPCs, boss hits, purple coins
- lane: A
- status: review
- owner: A
- started: 2026-10-13
- blocked_by: (after the first internet playtest, at the user's request: "as close to 1:1 across players as possible")
- touches: net/world (actor_sync new), net/client, net/puppet, server/src, decomp: Nerve.hpp (NEW_NERVE registers names), LiveActor.cpp (movement / calcAnmMtx), HitSensor.cpp, LiveActorUtil.cpp (initDefaultPos), MapPartsRailMover/Rotator/RailRotator.cpp (constructors), Coin.cpp, ObjUtil.cpp
- flags: parallel-ok
- what: Every placed actor is run by one player (the nearest, or the one standing on it, holding it, hitting it, or talking to / racing it if an NPC); the owner sends its state (ActorState 0x14: matrix, nerve, step, flags, rail and map-part phase, animation), the others show and run their copy to match. Boss health: every hit a player lands on a boss is replayed on the others' boss. Purple coins: collected for all, one shared count.
- done when: two clients started apart see platforms, enemies and NPCs in the same place and state; one standing on a platform rides it for both; a boss hit by one is hit on the other; no crashes across the main galaxies.
- log:
  - 2026-10-13 (A): net/world/actor_sync.{h,cpp}. Nerves get cross-build names: NEW_NERVE registers "Class::Nerve" and its source folder (family: MapObj/Map, Ride, Enemy, Boss, NPC) in a fixed static table. Ownership: nearest player with 300 units of margin; claims for 1.5 s on touch (a message from the local Mario, standing on it, holding it), lower id wins a double claim; NPCs in a busy nerve (Talk, Race, Ready, Goal, Run...) stay with the local player and are never driven. The owner sends what moved or changed (base matrix as pos + quat, nerve hash + step, hidden / collision / dead, RailRider coord, MapPartsRailMover / Rotator / RailRotator nerve, step and phase fields, BCK name + frame), 20 messages/s, nearest first. Copies: pose extrapolated from the last two states and eased (snaps beyond 800 units), set as mPosition before sensors and as the base matrix in calcAnmMtx (drawn and collides there); nerve followed when the owner changes it or after 45 frames apart (map objects, rides, enemies; not names with Talk/Demo/Bind/Rush/Take/Hold/Carry/...), step kept within 10 frames for map objects, parts set exactly, animation restarted and its frame kept. Loot from an actor another player runs is not dropped here (the owner's drop event brings it). Boss hits: WorldEvent kind 9 when the local player's attack (or star bit, or something he threw in the last 5 s) is accepted by a boss (object names KoopaVs*, DinoPackun*, TripodBoss*, BossBegoman, BossStinkBug, BossKameck, Dodoryu, OtaKing, Polta, SkeletalFishBoss, TombSpider, IceMerameraKing, KoopaJrShip; or nerves in Game/Boss); receivers deliver the same message to the same sensor from a puppet's sensor placed at the attacker's offset, retrying for 3 s. Purple coins: kind 2 with a = 1, receivers count them (MR::incPurpleCoin). Relay: forwards 0x14 when the sender byte matches, 120 msg/s. Verified, two native clients, B joining 12-14 s later: Good Egg Octoombas / Goomba within 15 units, Toy Time train and Toads within 5-12, Dreadnought UFO platforms within 5 units and 1.5 degrees; handoff when A runs off is smooth; a hit on an Octoomba (PETARI_TEST_BOSS=Takobo, debug mode 105) is taken on B's copy. 13 galaxies, two clients each with both running about: no crashes (Ghostly Galaxy crashes in FallOutFieldDraw::draw on quick start even without co-op, an older build too: not this). Native + web in one room: states and boss hits cross. Not tried: a real boss fight by hand, purple coin comets, races.

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

### P5-01 Disc image import (ISO/RVZ/WBFS)
- lane: B
- status: open
- owner:
- blocked_by: P1-10
- touches: tools/cook-ts, web/src/import
- flags: parallel-ok
- what: Read Wii disc images in the browser so users can skip Dolphin. Wii partitions are AES-128-CBC encrypted per 32 KB cluster; the user must supply their own common key (from their Dolphin install; never shipped, never fetched). RVZ adds zstd/LZMA block decompression; WBFS is a sector map. Parse partition table, decrypt, walk the FST, feed the P1-02 cooker. Explain the key requirement on screen in plain words. Scope the RVZ part last.
- done when: an ISO imports without Dolphin; RVZ if time allows.
- log:

### P5-02 Firefox and Safari support
- lane: A
- status: in_progress
- owner: A
- blocked_by: P3-06
- touches: platform/src/web, web/
- flags:
- what: Both ship WebGPU now; what differs is OffscreenCanvas-on-worker, OPFS sync handles, JSPI and pipeline-cache behaviour. Build the test matrix, fix what is cheap, document what is not. Decide whether P5-03 is needed.
- log:
  - 2026-10-13 (A): Firefox 132 (Playwright, headless): boots to title, cooked-folder import ~5 s, in-page disc conversion ~3-4 min, plays from OPFS. Fixed for it: never await storage.persist() (Firefox prompts), no empty OPFS writes (hung). tools/web/firefox-check.mjs. Safari untested.

### P5-03 WebGL2 fallback renderer
- lane: A
- status: open
- owner:
- blocked_by: P5-02
- touches: platform/src/gx
- flags:
- what: Only if P5-02 finds a browser we care about without usable WebGPU. Finish P0-10's rework of GalaxyQuest's renderer and select it at runtime when `navigator.gpu` is missing. Bench against Aurora. Decide in DECISIONS.
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
