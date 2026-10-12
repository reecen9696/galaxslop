// The co-op session (P2-03): joins a room on the relay, sends the local
// player's state at 30 Hz, buffers everyone else's and interpolates them
// 100 ms behind (docs/NETCODE.md "Client behaviour").
//
// Native configuration (environment):
//   SMG_RELAY  ws://host:port of the relay (unset: no networking)
//   SMG_ROOM   room code to join; unset or empty creates a room
//   SMG_NAME   player name (default "Player")
#pragma once

#include <stdint.h>

#include "net/capture/player_capture.h"

namespace net {

constexpr int kMaxRemotes = 4;  // 5 players per room, minus yourself

struct RemoteView {
    bool present = false;   // in the room
    uint8_t id = 0;
    char name[33] = {};
    bool luigi = false;
    bool visible = false;   // has fresh state for the local stage
    NetPlayerCapture pose;  // interpolated, in capture terms (baseMtx, weights...)
};

// Whether a relay is configured (SMG_RELAY).
bool netEnabled();
// Once per gameplay frame on the game thread: connects on first use (from a
// helper thread), sends, receives, interpolates.
void netTick(const NetPlayerCapture* local);
// Slot i (0..kMaxRemotes-1) as of the last netTick.
const RemoteView& netRemote(int slot);
// The room code once joined (empty before).
const char* netRoomCode();

}  // namespace net
