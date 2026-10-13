# Building and running

What works today (2026-10-12): the native desktop build on macOS boots a
cooked disc and plays it in a window with sound: menus, the prologue and
Good Egg Galaxy verified. The browser build is P0-06 onward.

## 1. Your own disc data

Follow `docs/GETTING_YOUR_DISC.md`, then cook it:

```
python3 tools/cook/cook.py ~/smg/extracted ~/smg/game
python3 tools/cook/cook.py --verify ~/smg/game
```

~10 s, ~1 GB. Keep `~/smg/` outside the repo.

## 2. Native build (macOS)

Needs: Xcode command line tools (clang), CMake 3.24+, Ninja
(`brew install ninja`), Rosetta (`softwareupdate --install-rosetta` if you
have never run an Intel app). SDL3 is downloaded and built by CMake.

```
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build/native smg          # first build ~3 min, then incremental
./build/native/smg --game ~/smg/game
```

The binary is **x86_64 and runs under Rosetta** on Apple Silicon. That is
deliberate: the game keeps pointers in 32-bit fields and must load at
0x98000000, which arm64 macOS forbids (D-016). The GPU is used natively
through Apple's OpenGL 4.1 (on Metal).

### Controls (keyboard + mouse)

| Wii | Key |
|---|---|
| Nunchuk stick | W A S D |
| A (jump) | Space |
| B | K, right click |
| Spin (shake) | J, Left Shift, left click |
| Z (crouch) | Ctrl |
| C | C |
| + / − | Enter or Esc / Backspace |
| 1 / 2 | 1 / 2 |
| D-pad | arrow keys |
| Home | H |
| Pointer | mouse over the window |
| Quit | F12 or close the window |

On the title screen press **Space and K together** (A + B).

### Useful flags

```
--save <dir>          NAND save folder (default: <game>/../save)
--scale N             render at N x 640x456 (default 2)
--headless            hidden window, 60 s, exit 0 (CI smoke test)
--seconds N           stop after N seconds
--shots <dir>         write frame_NNN.png every --shot-every ms (default 2000)
```

GalaxyQuest's debug switches work through the environment, for example:

```
PETARI_STAGE=EggStarGalaxy:1   # jump to a galaxy 2 s into gameplay
PETARI_INPUT="28:A|B,33:A"     # scripted input (see platform/include/port/input_script.h)
PETARI_PERFLOG=1               # frame timing
PETARI_GLITEMS=60              # log the renderer's batches every 60th frame
```

The full list is in `docs/galaxyquest/TECHNICAL.md`.

### Scripted boot to a galaxy (fresh save, no human)

Creates file 1 with the Toad icon, plays through the prologue by tapping A,
then jumps to Good Egg. Pointer positions are for the default 640x456 EFB.

```
MENU="28-28.4:A|B,30-36:PX=-0.25|PY=0.27,33-33.2:A,36-42:PX=0.29|PY=0.59,42-42.2:A,45-45.2:A,48-66:PX=0.55|PY=-0.38,66-77:PX=0.29|PY=0.59,77-300:PX=0.52|PY=0.78"
ICON=",58-58.2:A,61-61.2:A,64-64.2:A"
REST=$(for t in $(seq 70 3 290); do printf ",%s-%s.2:A" $t $t; done)
PETARI_STAGE=EggStarGalaxy:1 PETARI_INPUT="$MENU$ICON$REST" \
  ./build/native/smg --game ~/smg/game --save /tmp/smg-save --headless --seconds 300 --shots /tmp/shots --shot-every 6000
```

The pointer must hover before a press: a press whose entry also sets the
pointer arrives before the cursor is over the button.

## Co-op (native, early)

Two or more desktop builds can already see each other as Mario or Luigi
(P2-01..P2-05; the browser version comes later). One machine runs the
relay; everyone points their game at it and joins the same room.

```
# on the machine hosting (needs Node 22+):
cd server && npm install && ROOMS=PLAY22 npm run dev      # ws://<its-ip>:8787

# every player (the host too):
SMG_RELAY=ws://<host-ip>:8787 SMG_ROOM=PLAY22 SMG_NAME=Reece ./build/native/smg --game ~/smg/game
```

- Same Wi-Fi: use the host's LAN IP (`ipconfig getifaddr en0` on a Mac).
  Over the internet: expose port 8787 with a tunnel (`cloudflared tunnel
  --url http://localhost:8787` gives a URL; use it as `ws://...`), until
  P3-05 deploys the relay.
- Without `ROOMS`, leave `SMG_ROOM` unset on the first player: the relay
  makes a room and the game logs `net: joined room ABC123`; the others use
  that code.
- Others appear only when you are in the same stage and scenario, and not
  during cutscenes. They are drawn 100 ms in the past, as the character
  their save plays, in a shirt/cap colour fixed by player number (1 normal,
  2 green, 3 pink, 4 orange, 5 purple), the same on every screen including
  your own; `SMG_TINT=<n>` picks yours (values in `docs/NETCODE.md`).
  `tools/coop_two_players.sh` runs two clients side by side on one Mac,
  straight into Good Egg in ~20 s (`PETARI_QUICKSTART=Galaxy[:scenario]`
  skips the title, file select and prologue; `PETARI_QUICKSTART_LUIGI=1`).
  In a window F1-F8 switch your power-up form (normal, rainbow, fire, ice,
  bee, spring, boo, flying), F9 squashes you, F10 defeats the nearest
  enemy and F11 drops coins and star bits (world sync tests); `PETARI_DEBUG_MODES` schedules
  them. Ground-pound another player to squash them (one health); jumping on
  them just bounces you and makes them flinch.
  `PETARI_LOOPBACK=0 PETARI_LOOPBACK_SIDE=110` draws a copy of yourself
  beside you, for comparing the puppet with the real player. Nothing else is shared yet (no stars, no enemies).
- `PETARI_NETLOG=1` logs the room, round-trip time and where the others are.
  `PETARI_LOOPBACK=1` shows a copy of yourself 1 s behind, with no network
  (green, or by `SMG_TINT`).

## 3. Linux

The CMake path exists (`-no-pie -Wl,-Ttext-segment=0x98000000`) but
`platform/include/port/gl.h` has no desktop-GL configuration for Linux yet.
Not tried.

## 4. Web

See [`docs/WEB.md`](WEB.md) for building and running the browser version.

## 5. What the build is made of

- `decomp/`: Petari `0a5c660` + GalaxyQuest's `TARGET_PC` port.
- `platform/`: GalaxyQuest's Wii replacement layer; `platform/src/native/`
  is ours (launcher, SDL3 presenter, flat-screen stubs for the VR hooks).
- `platform/src/gx/`: GalaxyQuest's GX pipeline. Petari's SDK GX writes a
  hardware FIFO, `gpu_cp.cpp` emulates the command processor,
  `gx_recorder.cpp` batches, `gl_renderer.cpp` draws. Ported from GLES 3.2
  to the ES 3.0 / GL 4.1 common subset (D-008, D-017).
- Android/Quest files (`platform/android/`, `platform/src/xr/`,
  `build_android.sh`) are still in the tree but not built on desktop.
