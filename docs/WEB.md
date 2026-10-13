# The browser build

The game compiles to WebAssembly with Emscripten and runs in Chrome over
WebGL2 (P0-06, P0-08). Two variants share `platform/src/web/main_web.cpp`:

| CMake option | Runs in | Game files | Video |
|---|---|---|---|
| `-DSMG_WEB_NODE=ON` (default) | Node | host file system (NODERAWFS) | none (null GPU backend) |
| `-DSMG_WEB_NODE=OFF` | the browser | dev server, WasmFS fetch backend | WebGL2 on `#canvas` |

## Build

```sh
git clone https://github.com/emscripten-core/emsdk ~/emsdk
~/emsdk/emsdk install latest && ~/emsdk/emsdk activate latest
source ~/emsdk/emsdk_env.sh

# browser
emcmake cmake -S . -B build/web -G Ninja -DCMAKE_BUILD_TYPE=Release -DSMG_WEB_NODE=OFF
ninja -C build/web smg            # build/web/smg.{js,wasm}, ~4 min from clean

# Node bring-up
emcmake cmake -S . -B build/web-node -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build/web-node smg
node build/web-node/smg.js --game ~/smg/game --seconds 30
```

## Run in Chrome

```sh
node web/dev-server.mjs 8080           # page only
# open http://localhost:8080/
```

The first visit shows an import screen: pick the folder `tools/cook/cook.py`
wrote (it must contain `manifest.json`; your own disc, never committed). The
page checks every file against the manifest and copies the folder into the
browser's origin-private file system (OPFS, about 6 s for 1 GB); after that
the game starts straight from OPFS on every visit. Saves are written to OPFS
too (`/smg/save`), so they survive reloads. `?reimport=1` forgets the files
and asks again.

For development the game can read a folder from the dev server instead:

```sh
SMG_GAME=~/smg/game node web/dev-server.mjs 8080
# open http://localhost:8080/?source=server
```

The dev server sends the COOP/COEP headers threads need
(`crossOriginIsolated` must be true) and answers HTTP range requests, which the
WasmFS fetch backend uses to read each file on first use.

Controls are the desktop ones (keyboard + mouse over the canvas; right click
is B). URL parameters named `PETARI_*` or `SMG_*` become environment
variables, for example `?PETARI_INPUT=28000:A|300:-` or `?PETARI_PERFLOG=1`.
`?PETARI_TIMER=1` paces frames with `setTimeout`: Chrome pauses
`requestAnimationFrame` in hidden tabs.

## Smoke test

```sh
node tools/web/smoke.mjs --url http://localhost:8080/ --runs 10 --seconds 13
node tools/web/smoke.mjs --url http://localhost:8080/ --seconds 40 --shot title.png
node tools/web/smoke.mjs --url http://localhost:8080/ --import ~/smg/game --profile /tmp/p   # OPFS path
```

It boots the page in a private headless Chrome (its own temporary profile)
and fails a run if the page logs `no new game frame`: `main_web.cpp` dumps
every OS thread with its last `port_debug_where` breadcrumb when the game
stops producing frames for 5 s. WebAssembly has no remote backtraces, so the
breadcrumbs are how a hang is located.

## How it fits together

- `main()` runs on a worker (`PROXY_TO_PTHREAD`); the canvas is transferred
  to it as an OffscreenCanvas. The frame callback (`emscripten_set_main_loop`)
  reads input, then renders the latest recorded frame with the desktop
  `gpu::Renderer` (GLSL ES 3.00 via `port/gl.h`).
- One wasm memory holds the game image at `0x98000000`, MEM1/MEM2 and the low
  pool (D-019).
- Game files and saves: WasmFS with the OPFS backend mounted at `/opfs`
  (`web/app.js` does the import on the page's main thread).
- Still to do: audio worklet, gamepad, the WebSocket transport for co-op
  (P2-03 web half), save export/import, the in-browser cooker (P1-02).

## Lessons

- A host mutex must never be held across anything that can reach an OS safe
  point (a game-heap allocation, `OSDisableInterrupts`...): the safe point can
  switch game threads, and the next thread that wants the mutex blocks while
  holding the virtual CPU. Port code that takes a host lock on a game thread
  allocates inside a `PortHostAllocScope`. (Found as a 1-in-10 boot hang in
  the browser: `DVDGetDriveStatus` from JAudio's stream callback.)
- Busy-wait loops in game code need a `port_irq_poll()` safe point under
  `TARGET_PC`; the port delivers interrupts only at safe points.
