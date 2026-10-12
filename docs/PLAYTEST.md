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
- [ ] native (Aurora/Dawn): title screen renders with real disc
- [ ] native: Good Egg matches Dolphin side by side (screenshot pair in the log)
- [ ] web (Aurora/WebGPU): title screen renders with real disc from OPFS
- [ ] P0-11 go/no-go logged with frame time at 720p
- [ ] D-005 decided with a measured reason (worker + OffscreenCanvas vs main thread)
- [ ] D-013 decided with resident-memory numbers

## Phase 1 gate (Chromium, reference machine, gamepad)
- [ ] import EUR ISO in under 10 min, manifest verifies
- [ ] new game, prologue plays with sound
- [ ] pointer sections (star bits, pull stars) work with stick and mouse
- [ ] reach Good Egg, get first star
- [ ] save, reload tab, continue from save
- [ ] frame time p95 under 16 ms in Good Egg at 1x
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
