# Third-party notices

- Petari (SMGCommunity): CC0-1.0. Vendored in `decomp/`. See `decomp/LICENSE-Petari.txt`.
- GalaxyQuest (bigmak94): The Unlicense. `decomp/` port changes, `platform/` (OS, DVD, NAND, audio, input, port glue; its GLES renderer kept as the WebGL2 fallback) and `tools/cook/`, vendored as one snapshot at commit `d8a039c96f89c188fc44cdb7382ab9a4cf9ce92e` (D-007). Its `xr/`-only dependencies (AMD FidelityFX CAS, MIT; Inter font, OFL 1.1; OpenXR loader, Apache 2.0; Android native app glue, Apache 2.0) are present inside `platform/src/xr/` and `platform/android/` until P0-03 deletes both.
- Aurora (encounter): MIT. The GX layer, pinned submodule in `extern/aurora` (D-014). Through its own providers it brings: Dawn (BSD-3-Clause), SDL3 (zlib), nod (MIT), kabufuda (MIT), zstd (BSD-3-Clause), magic_enum (MIT), Dear ImGui (MIT). Record the pinned Aurora commit in `DECISIONS.md` when P0-11 lands.
- emdawnwebgpu (Dawn's Emscripten WebGPU bindings): BSD-3-Clause. Web build only, via the Emscripten port.
- coi-serviceworker (gzuidhof): MIT. `web/public/coi-serviceworker.js` (planned).
- doctest: MIT. `platform/third_party/doctest/` (planned).

Read for approach, nothing copied (GPL-3, incompatible with this repo's Unlicense): webcompiled (dynamiteswag81), WiiCompiled (pgattic). Read for design only, no license on the game-side code: smg-online's `SMGNetworkMultiplayer/` (Headpenguin).

Add an entry for every new dependency, and a DECISIONS.md line explaining why.

Super Mario Galaxy and Mario are trademarks of Nintendo. This project contains no Nintendo game data.
- M PLUS 1p (The M+ Project Authors): SIL Open Font License 1.1. Latin subset in `web/fonts/`, license in `web/fonts/OFL.txt` (D-023).
- three.js 0.169.0 (three.js authors): MIT. Core build and the FBX loader with its imports, in `web/vendor/three/`, license in `web/vendor/three/LICENSE` (D-024).
