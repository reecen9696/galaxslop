#!/bin/bash
# Two co-op clients on this machine, side by side, scripted onto the same
# spot in Good Egg; after that the keyboard drives whichever window has focus.
#   tools/coop_two_players.sh [game dir]        (default ~/smg/game)
# Starts its own relay (port $PORT, room TEST01), fresh saves in a temp dir.
# The script creates file 1 and taps A through the prologue: keep the mouse
# off both windows until Mario is standing in Good Egg (~4 min).
# Env passes through: SMG_TINT_A / SMG_TINT_B pick colours (docs/NETCODE.md).
cd "$(dirname "$0")/.."
GAME=${1:-$HOME/smg/game}
PORT=${PORT:-8799}
ROOM=TEST01
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

# The fresh-save boot from docs/BUILDING.md: file 1 with the Toad icon, the
# prologue by tapping A, then PETARI_STAGE jumps to Good Egg (~130-180 s
# in; a window that renders slower gets there later).  A taps (Mario hops)
# go on until 240 s so the slower window still gets through; then it's yours.
MENU="28-28.4:A|B,30-36:PX=-0.25|PY=0.27,33-33.2:A,36-42:PX=0.29|PY=0.59,42-42.2:A,45-45.2:A,48-66:PX=0.55|PY=-0.38,66-77:PX=0.29|PY=0.59,77-240:PX=0.52|PY=0.78"
ICON=",58-58.2:A,61-61.2:A,64-64.2:A"
TAPS=$(for t in $(seq 70 3 240); do printf ",%s-%s.2:A" $t $t; done)

launch() {  # name, window x, tint
  SMG_RELAY=ws://127.0.0.1:$PORT SMG_ROOM=$ROOM SMG_NAME=$1 SMG_TINT=$3 SMG_WINDOW="$2,40,715,450" \
  PETARI_STAGE=EggStarGalaxy:1 PETARI_INPUT="$MENU$ICON$TAPS" \
    "$SMG" --game "$GAME" --save "$WORK/save-$1" > "$WORK/$1.log" 2>&1 &
  PIDS+=($!)
}
launch Alice 0 "${SMG_TINT_A:-0}"
launch Bob 725 "${SMG_TINT_B:-0}"
echo "two windows opening; close both (or Ctrl-C here) to stop"
wait "${PIDS[1]}" "${PIDS[2]}"
