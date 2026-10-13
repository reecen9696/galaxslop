// Clothes colours for remote player puppets (P2-05).  Pure C++, no game
// dependency (unit-tested in net_tests).
//
// Mario's and Luigi's shirt and cap colour is baked into two CMPR textures
// (MarioBody.0, MarioCap.0 in Mario.arc / Luigi.arc).  A tint rewrites the
// endpoint colours of the CMPR blocks that are the character's own colour
// (red for Mario, green for Luigi) to another hue, so overalls, buttons,
// shoes, skin and the cap emblem keep theirs.
//
// Tint numbers are what Hello's `tint` byte carries (docs/NETCODE.md).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace net {

enum : uint8_t {
    kTintAuto = 0,     // by player number (puppetTint)
    kTintClassic = 1,  // the character's own colours
    kTintGreen = 2,    // red instead, on Luigi
    kTintPink = 3,
    kTintOrange = 4,
    kTintPurple = 5,
    kTintCyan = 6,
    kTintYellow = 7,
    kTintWhite = 8,
    kTintBlack = 9,
    kTintCount = 10,
};

// The colour a player is drawn in, on every client including their own:
// their Hello choice (`requested`), else by relay player id (1-based):
// player 1 classic, 2 green, 3 pink, 4 orange, 5 purple, then cyan, yellow,
// white, black.  Id 0 (not in a room) is classic.
uint8_t puppetTint(uint8_t requested, uint8_t playerId);

// Recolours a big-endian GX CMPR image (`bytes` = width * height / 2) from
// `src` into `dst` (may be the same buffer).  Classic or out-of-range tints
// copy unchanged.
void tintCmpr(const uint8_t* src, uint8_t* dst, size_t bytes, bool luigi, uint8_t tint);

}  // namespace net
