// Unit tests for puppet clothes tints (P2-05, net/puppet/puppet_tint.h).
#include <doctest/doctest.h>

#include <string.h>

#include "net/puppet/puppet_tint.h"

using namespace net;

namespace {
// One CMPR block: big-endian RGB565 endpoints, then 2-bit indices.
void block(uint8_t* b, uint16_t c0, uint16_t c1, uint8_t idx) {
    b[0] = (uint8_t)(c0 >> 8);
    b[1] = (uint8_t)c0;
    b[2] = (uint8_t)(c1 >> 8);
    b[3] = (uint8_t)c1;
    memset(b + 4, idx, 4);
}
uint16_t c0(const uint8_t* b) { return (uint16_t)(b[0] << 8 | b[1]); }
uint16_t c1(const uint8_t* b) { return (uint16_t)(b[2] << 8 | b[3]); }
int red(uint16_t c) { return c >> 11; }
int green(uint16_t c) { return (c >> 5) & 63; }
int blue(uint16_t c) { return c & 31; }

const uint16_t kRed = 0xE000;        // Mario's shirt (230, 0, 0)
const uint16_t kDarkRed = 0x9000;    // its shading
const uint16_t kGreen = 0x2D40;      // Luigi's shirt
const uint16_t kOveralls = 0x0190;   // blue
const uint16_t kShoe = 0xA2A5;       // brown, hue ~25 degrees
}  // namespace

TEST_CASE("auto tint is fixed by player number") {
    CHECK(puppetTint(kTintAuto, 0) == kTintClassic);
    CHECK(puppetTint(kTintAuto, 1) == kTintClassic);
    CHECK(puppetTint(kTintAuto, 2) == kTintGreen);
    CHECK(puppetTint(kTintAuto, 3) == kTintPink);
    CHECK(puppetTint(kTintAuto, 4) == kTintOrange);
    CHECK(puppetTint(kTintAuto, 5) == kTintPurple);
    CHECK(puppetTint(kTintAuto, 10) == kTintClassic);  // wraps
    CHECK(puppetTint(kTintCyan, 1) == kTintCyan);      // a choice wins
    CHECK(puppetTint(200, 2) == kTintGreen);           // unknown -> auto
}

TEST_CASE("classic copies unchanged") {
    uint8_t src[16], dst[16];
    block(src, kRed, kOveralls, 0x1B);
    block(src + 8, kGreen, kShoe, 0xE4);
    tintCmpr(src, dst, sizeof(src), false, kTintClassic);
    CHECK(memcmp(src, dst, sizeof(src)) == 0);
}

TEST_CASE("green Mario: red endpoints turn green, the rest stay") {
    uint8_t b[24];
    block(b, kRed, kDarkRed, 0x1B);
    block(b + 8, kRed, kOveralls, 0x1B);
    block(b + 16, kShoe, 0x0000, 0x1B);
    uint8_t before[24];
    memcpy(before, b, sizeof(b));
    tintCmpr(b, b, sizeof(b), false, kTintGreen);
    for (int i = 0; i < 2; i++) {
        uint16_t c = c0(b + 8 * i);
        CHECK(green(c) > 2 * red(c));  // 6-bit green vs 5-bit red
        CHECK(green(c) > 2 * blue(c));
    }
    // The overalls endpoint is untouched; the shoe block is not clothes.
    bool overalls = c0(b + 8) == kOveralls || c1(b + 8) == kOveralls;
    CHECK(overalls);
    CHECK(memcmp(b + 16, before + 16, 8) == 0);
}

TEST_CASE("blocks keep their CMPR mode and texel colours stay in order") {
    // Four-colour block (c0 > c1) whose order flips once recoloured: dark
    // red (0x9000) is above mid grey, cyan (0x04xx) is below it.
    uint8_t b[8];
    const uint16_t grey = 0x8410;
    block(b, kDarkRed, grey, 0x1B);  // indices 0,1,2,3
    REQUIRE(kDarkRed > grey);
    tintCmpr(b, b, 8, false, kTintCyan);
    CHECK(c0(b) > c1(b));  // still four colours
    // The grey endpoint is unchanged and now sits where index 0 points.
    if (c0(b) == grey) {
        CHECK(b[4] == (0x1B ^ 0x55));
    } else {
        CHECK(c1(b) == grey);
        CHECK(b[4] == 0x1B);
    }

    // Three-colour block (c0 <= c1, index 3 transparent) stays three-colour
    // and keeps its transparent texels.
    block(b, 0x0000, kRed, 0xFF);  // all transparent
    tintCmpr(b, b, 8, false, kTintYellow);
    CHECK(c0(b) <= c1(b));
    CHECK(b[4] == 0xFF);
}

TEST_CASE("Luigi's green slot is red; Mario's red is left alone on Luigi") {
    uint8_t b[16];
    block(b, kGreen, 0x0000, 0x00);
    block(b + 8, kRed, 0x0000, 0x00);
    tintCmpr(b, b, sizeof(b), true, kTintGreen);
    uint16_t c = c0(b) > c1(b) ? c0(b) : c1(b);
    CHECK(red(c) * 2 > green(c));
    CHECK(blue(c) < 4);
    CHECK(c0(b + 8) == kRed);
}
