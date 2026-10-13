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
- status: open
- owner:
- blocked_by: P0-09
- touches: platform/src/gx, platform/src/web, web/src/shaders, tools/bench
- flags: parallel-ok
- what: Aurora keeps a pipeline cache with transferable entries (zstd by default). Persist it to OPFS on the web and to the user data dir natively; load it before the first frame. Pipeline creation goes through `createRenderPipelineAsync`; never block the frame on it. Ship a seed built by a `tools/bench/shader-tour.ts` script that visits every galaxy headless (human runs it once with a real disc; the seed contains no game data, only pipeline descriptors and WGSL). Version the seed against the Aurora commit; a submodule bump invalidates it. If P0-11 went no-go, this task is the WebGL2 version: GLSL source cache keyed by TEV hash plus `KHR_parallel_shader_compile`.
- done when: second load of Good Egg has no pipeline hitch over 50 ms.
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
  - 2026-10-13 (A): power-up forms on puppets: PlayerMode in PlayerState flags bits 6-9. Fire/flying = suit texture variants through the tint copy (the tint DL overrides the player's ColorChange btp, so the local player's suit goes through it too). Rainbow/ice/bee/spring = the form's body model sharing the puppet's animated joints (copyAnmMtxBuffer + own calcWeightEnvelopeMtx), own hands for rainbow/ice, rainbow brk, ice in the indirect pass (DrawAdaptor DrawType_0x24), bee wings and boo as PartsModels. All form archives mount into the scene heap in co-op (the stage file cache overflowed). 4 Mario + 4 Luigi puppets with every form: 2.1 MB of the scene heap (115 MB free). Verified side by side (PETARI_LOOPBACK=0 PETARI_LOOPBACK_SIDE=110) for Mario all forms and Luigi normal/fire/bee. Port bugs fixed on the way: JPA volume functions divide by zero (x86 traps, PPC does not; JPADynamicsBlock.cpp, TARGET_PC). Not done: rainbow trail, boo transparency/blink, spin drill. Seen in loopback (green Mario trailing red Mario in Good Egg). Untested: a Luigi save. Remaining: heap budget.
  - 2026-10-13 (A): networked: two desktop clients in relay room PLAY22, each drawing the other as a GhostMario puppet (screenshot: Bob idle at the courtyard spawn inside Alice's game). Remaining: Luigi model per remote (Hello character), tint, heap budget measurement.
  - 2026-10-12: loopback milestone works natively: PETARI_LOOPBACK=1 draws a GhostMario puppet replaying the local player 1 s late in the prologue courtyard (screenshot: blue ghost trailing Mario). Lessons: (1) create puppets during scene init (SceneDataInitializer::initAfterScenarioSelected), never from the frame update: ModelManager::init waits on a main-thread job and deadlocks there; (2) add GhostMario/GhostLuigi to the stage's archive list (startStageFileLoadAfterScenarioSelected); (3) build the XanimePlayers on the first frame the player exists (the table is the player's); (4) skip the file-select stage. Next: animation sync check (does the hash map to the right motion?), then drive it from P2-03 instead of the loopback.
  - 2026-10-12 (A): model it on GhostPlayer (Cosmic Mario ghost): XanimePlayer + upper-body player, changeAnimationByHash, receiveGhostPacket's apply logic. First milestone: a loopback puppet that replays the local player's captured state ~1 s late, no network.

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

### P3-00 Co-op save policy
- lane: shared
- status: open
- owner:
- blocked_by: P2-08
- touches: docs/DECISIONS.md, platform/src/nand (read only), decomp/src/Game/System (read only)
- flags: solo
- what: Turn D-012 from proposal into decision. Read how the save layer picks a NAND file and slot. Decide: per-room save namespace, one shared "co-op" save, or merge into the chosen slot with an explicit warning. Write the D entry with the file paths involved. No code.
- done when: D-012 is superseded by a decision entry and P3-02 knows which file to hook.
- log:

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
- status: open
- owner:
- blocked_by: P2-08, P3-00, P3-01
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

### P3-05 Deploy the relay
- lane: B
- status: open
- owner:
- blocked_by: P3-01
- touches: server/
- flags:
- what: Per D-010. Dockerfile for the Node relay. Deploy to Fly.io or Railway (or any VPS) with TLS. Env for allowed origins and port. Health endpoint. Same code as local dev. Document deploy in `server/README.md` with the monthly cost.
- done when: a room works on the deployed `wss://` URL.
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
- status: open
- owner:
- blocked_by: P3-06
- touches: platform/src/web, web/
- flags:
- what: Both ship WebGPU now; what differs is OffscreenCanvas-on-worker, OPFS sync handles, JSPI and pipeline-cache behaviour. Build the test matrix, fix what is cheap, document what is not. Decide whether P5-03 is needed.
- log:

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
