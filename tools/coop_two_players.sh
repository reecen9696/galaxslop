#!/bin/bash
# Two co-op clients on this machine, side by side, booted straight into the
# same galaxy (PETARI_QUICKSTART: no title, file select or prologue; ~20 s to
# gameplay); the keyboard drives whichever window has focus.
#   tools/coop_two_players.sh [game dir]        (default ~/smg/game)
# Starts its own relay (port $PORT, room TEST01), fresh saves in a temp dir.
# Environment:
#   GALAXY=EggStarGalaxy:1    galaxy[:scenario] both players start in
#   LUIGI_A=1 / LUIGI_B=1     play that client as Luigi
#   SMG_TINT_A / SMG_TINT_B   colour override (docs/NETCODE.md)
#   INPUT_A / INPUT_B         PETARI_INPUT scripts (platform/include/port/input_script.h)
#   MODES_A / MODES_B         PETARI_DEBUG_MODES "seconds:form,..." (seconds after
#                             gameplay starts; forms as F1-F8: 0 normal,
#                             1 rainbow, 2 fire, 3 ice, 4 bee, 5 spring, 6 boo,
#                             7 flying).  In a window F1-F8 do the same.
#   HEADLESS=1 RUN_SECONDS=60 SHOTS=<dir>   unattended run: hidden windows,
#                             stop after RUN_SECONDS, screenshots every 2 s
#                             into <dir>/a and <dir>/b (SHOT_EVERY=<ms>)
cd "$(dirname "$0")/.."
GAME=${1:-$HOME/smg/game}
PORT=${PORT:-8799}
ROOM=TEST01
GALAXY=${GALAXY:-EggStarGalaxy:1}
SMG=./build/native/smg
[ -x "$SMG" ] || { echo "build first: cmake --build build/native" >&2; exit 1; }
[ -d "$GAME" ] || { echo "no cooked game at $GAME" >&2; exit 1; }
if lsof -i :"$PORT" -sTCP:LISTEN >/dev/null 2>&1; then
  echo "port $PORT is in use; PORT=<other> $0" >&2
  exit 1
fi

WORK=$(mktemp -d -t smg-coop)
echo "saves and logs: $WORK"
(cd server && PORT=$PORT ROOMS=$ROOM exec node --experimental-strip-types src/main.ts) > "$WORK/relay.log" 2>&1 &
PIDS=($!)
trap 'kill "${PIDS[@]}" 2>/dev/null' EXIT INT TERM
sleep 1

launch() {  # name, window x, tint, luigi, input, shots subdir, debug modes
  local args=(--game "$GAME" --save "$WORK/save-$1")
  if [ -n "$HEADLESS" ]; then
    args+=(--headless --seconds "${RUN_SECONDS:-60}")
  fi
  if [ -n "$SHOTS" ]; then
    mkdir -p "$SHOTS/$6"
    args+=(--shots "$SHOTS/$6" --shot-every "${SHOT_EVERY:-2000}")
  fi
  SMG_RELAY=ws://127.0.0.1:$PORT SMG_ROOM=$ROOM SMG_NAME=$1 SMG_TINT=$3 SMG_WINDOW="$2,40,715,450" \
  PETARI_QUICKSTART=$GALAXY PETARI_QUICKSTART_LUIGI=$4 PETARI_INPUT=$5 PETARI_DEBUG_MODES=$7 \
    "$SMG" "${args[@]}" > "$WORK/$1.log" 2>&1 &
  PIDS+=($!)
}
launch Alice 0 "${SMG_TINT_A:-0}" "${LUIGI_A:-0}" "$INPUT_A" a "$MODES_A"
sleep 1  # usually Alice joins first (player 1, left window)
launch Bob 725 "${SMG_TINT_B:-0}" "${LUIGI_B:-0}" "$INPUT_B" b "$MODES_B"
echo "two clients starting in $GALAXY; close both windows (or Ctrl-C here) to stop"
wait "${PIDS[1]}" "${PIDS[2]}"
