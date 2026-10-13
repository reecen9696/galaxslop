// The interact key (E) for characters the port adds
// (decomp/src/Game/NPC/BarneyNPC.cpp), which have no message data on disc and
// so no place in the game's own talk flow.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Platform side: E went down (not a key repeat).
void port_talk_note_press(void);
// Game side: whether E was pressed since the last call (each press once).
int port_talk_take_press(void);

#ifdef __cplusplus
}
#endif
