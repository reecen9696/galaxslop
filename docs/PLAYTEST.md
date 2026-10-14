# Playtest checklists

One section per phase gate. Run with a real cooked disc. Paste results into
the gate task's `log` in TASKS.md. Mark each line pass / fail / skipped
with a one-line note on failure.

## P0-00: smg-online in Dolphin (before any code)
Date, players, how long. Then: what felt good; where puppets snapped and
at what ping; what the save sharing did that surprised you; what you
missed (voice? name tags? follow-host?); what you would cut. One paragraph
each. This list reorders Phases 2-4.

## Phase 0 gate
- [ ] native: `--headless --frames 600` exits 0 on stub folder (null renderer)
- [ ] web: same, in Playwright-Chromium
- [x] native (GL 4.1, macOS): title screen renders with real disc (2026-10-12, RMGP01)
- [ ] native: Good Egg matches Dolphin side by side (screenshot pair in the log). Good Egg renders correctly by eye (2026-10-12: terrain, foliage alpha, water, sky, HUD, 0 shader errors); Dolphin comparison not done.
- [x] web (WebGL2): title screen renders with real disc from OPFS (2026-10-13, headless Chrome via tools/web/smoke.mjs; a human look is still wanted)
- [x] P0-11 go/no-go logged (no-go, D-017)
- [ ] D-005 decided with a measured reason (worker + OffscreenCanvas vs main thread)
- [ ] D-013 decided with resident-memory numbers

## Phase 1 gate (Chromium, reference machine, gamepad)
- [ ] import EUR ISO in under 10 min, manifest verifies (automated 2026-10-13: Dolphin-extracted EUR folder converts in the page in 36 s, byte-identical to cook.py; ISO/RVZ directly is P5-01)
- [ ] new game, prologue plays with sound
- [ ] pointer sections (star bits, pull stars) work with stick and mouse
- [ ] reach Good Egg, get first star
- [ ] save, reload tab, continue from save (automated: save files persist in OPFS across reloads)
- [ ] frame time p95 under 16 ms in Good Egg at 1x (headless 2026-10-13: interval 16.7/17.9 ms p50/p95; settings > "Show frame times" on the reference machine)
- [ ] second load of Good Egg: no shader hitch over 50 ms
- [ ] 10 min of play: no audio crackle, no crash

## Phase 2 gate (3 players, internet)
- [ ] all 3 join by room code without help
- [ ] each sees the other 2 move in Good Egg
- [ ] puppets freeze/hide/reappear correctly on a deliberate 5 s disconnect
- [ ] puppets not drawn when in a different galaxy
- [ ] name tags track, player list correct
- [ ] 30 min, no crash, RTT and snapping notes recorded

## Phase 3 gate (5 players, 2 hours)
- [ ] 5 in a room
- [ ] a star got by one shows in all saves after reload
- [ ] late joiner (join at 1 h) sees correct Observatory state
- [ ] follow-host warp lands you with the host
- [ ] deployed relay, not a tunnel
- [ ] nobody lost a star; log of all stars vs each client's save matches

## Browser co-op quick test (any time, two tabs or two machines)
1. `cd server && PORT=8787 STATIC_DIR=../dist/web npm start` after
   `node tools/web/bundle.mjs`, open http://localhost:8787/ in two windows
   (or two machines on the LAN: use the host's address).
2. First window: load your disc (extracted folder or cooked folder), then
   "Create a room". Note the code under the canvas.
3. Second window (a second browser profile, so it has its own storage):
   load the disc, enter the code, "Join room".
- [ ] both see each other on the title/file select in the room bar
- [ ] in the same galaxy, each sees the other's Mario/Luigi and name tag
- [ ] one gets a star: the other gets a toast and the star on their save
- [ ] one enters another galaxy: the other's room bar shows it; Go to
      takes them there, beside the other player
- [ ] in the same galaxy, Go to puts you beside that player
- [ ] contact: walking into each other pushes, landing on a head bounces
      (lobby toggle off disables it)

## Phase 3 gate additions
- [ ] a player who joins later receives every star and flag already logged
- [ ] Go to works from the Observatory and from inside a galaxy, and into
      the Observatory
- [ ] three players back from a grand star: each one's dome door opens in
      their own return cutscene and each can walk in (no reload)
- [ ] Gateway: three players catching one bunny each finish the hunt
