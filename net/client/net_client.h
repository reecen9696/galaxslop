// The co-op session (P2-03): joins a room on the relay, sends the local
// player's state at 30 Hz, buffers everyone else's and interpolates them
// 100 ms behind (docs/NETCODE.md "Client behaviour").
//
// Native configuration (environment):
//   SMG_RELAY  ws://host:port of the relay (unset: no networking)
//   SMG_ROOM   room code to join; unset or empty creates a room
//   SMG_NAME   player name (default "Player")
//   SMG_TINT   puppet colour others see you in (net::kTint*; default auto)
#pragma once

#include <stdint.h>

#include "net/capture/player_capture.h"
#include "net/proto/player_state.h"

namespace net {

constexpr int kMaxRemotes = 4;  // 5 players per room, minus yourself

struct RemoteView {
    bool present = false;   // in the room
    uint8_t id = 0;
    char name[33] = {};
    bool luigi = false;     // from their state packets once any arrived, else Hello
    uint8_t tint = 0;       // requested in Hello (net::kTint*)
    bool visible = false;   // has fresh state for the local stage
    bool frozen = false;    // visible, but no new state for a while (held pose)
    NetPlayerCapture pose;  // interpolated, in capture terms (baseMtx, weights...)
};

// Whether a relay is configured (SMG_RELAY).
bool netEnabled();
// Once per frame on the game thread, in every scene (title and menus too,
// so the room is joined and its code known before gameplay): connects on
// first use (from a helper thread) and receives.  True while connected.
bool netPump();
// Once per gameplay frame on the game thread: netPump, then sends the local
// player and interpolates the others.
void netTick(const NetPlayerCapture* local);
// Slot i (0..kMaxRemotes-1) as of the last netTick.
const RemoteView& netRemote(int slot);
// Contact (D-020): tell player `targetId` this client touched them.
void netSendContact(uint8_t targetId, uint8_t kind, const float dir[3]);
// Contacts other players made with the local player, oldest first; false
// when none are waiting.
bool netPollContact(Contact* out);
// The room code once joined (empty before).
const char* netRoomCode();
// This client's player id in the room (1-based; 0 before joining).
uint8_t netLocalId();

}  // namespace net

extern "C" void port_net_pump(void);  // net::netPump, for GameSystem::frameLoop
