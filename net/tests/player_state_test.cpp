// Unit tests for the PlayerState wire format (P2-01).  Built as net_tests;
// run with `ctest` or ./build/native/net_tests.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "net/proto/player_state.h"

using namespace net;

namespace {
// What the relay does: insert the sender id after the type byte.
void forward(const uint8_t* in, uint8_t id, uint8_t* out) {
    out[0] = in[0];
    out[1] = id;
    memcpy(out + 2, in + 1, kPlayerStateSize - 1);
}

float halfStep(float v) { return fmaxf(fabsf(v) / 1024.0f, 6.1e-5f); }
}  // namespace

TEST_CASE("half floats round-trip within half precision") {
    const float values[] = {0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 0.333333f, 65504.0f, 1e-5f, -2.75f, 123.456f};
    for (float v : values) {
        CHECK(fabsf(halfToFloat(floatToHalf(v)) - v) <= halfStep(v));
    }
    CHECK(isinf(halfToFloat(floatToHalf(1e9f))));
    CHECK(isnan(halfToFloat(floatToHalf(NAN))));
    CHECK(floatToHalf(1.0f) == 0x3C00);
    CHECK(floatToHalf(-2.0f) == 0xC000);
}

TEST_CASE("PlayerState round-trips through encode, relay forwarding, decode") {
    PlayerState s;
    s.tick = 123456;
    s.pos[0] = 13550.0f, s.pos[1] = -1300.25f, s.pos[2] = 11900.5f;
    float m[3][4] = {{0.0f, 0.0f, 1.0f, 0}, {0.0f, 1.0f, 0.0f, 0}, {-1.0f, 0.0f, 0.0f, 0}};
    matrixToQuat(m, s.rot);
    s.up[0] = 0.01f, s.up[1] = 0.999f, s.up[2] = -0.02f;
    s.vel[0] = 12.5f, s.vel[1] = -3.0f, s.vel[2] = 0.25f;
    s.animHash = 0xffca0c49;
    s.animWeights[0] = 0.0f, s.animWeights[1] = 0.13f, s.animWeights[2] = 0.87f, s.animWeights[3] = 1.0f;
    s.animFrame = 43.5f;
    s.flags = NET_STATE_GROUNDED | NET_STATE_LUIGI;
    s.stageHash = 0x1097f2ee;
    s.scenario = 3;

    uint8_t wire[kPlayerStateSize];
    encodePlayerState(s, wire);
    CHECK(wire[0] == kMsgPlayerState);
    uint8_t fwd[kPlayerStateForwardedSize];
    forward(wire, 4, fwd);

    uint8_t id = 0;
    PlayerState d;
    REQUIRE(decodeForwardedPlayerState(fwd, sizeof(fwd), &id, &d));
    CHECK(id == 4);
    CHECK(d.tick == s.tick);
    for (int i = 0; i < 3; i++) CHECK(d.pos[i] == s.pos[i]);  // f32: exact
    for (int i = 0; i < 4; i++) CHECK(fabsf(d.rot[i] - s.rot[i]) <= halfStep(s.rot[i]));
    for (int i = 0; i < 3; i++) CHECK(fabsf(d.vel[i] - s.vel[i]) <= halfStep(s.vel[i]));
    CHECK(d.animHash == s.animHash);
    for (int i = 0; i < 4; i++) CHECK(fabsf(d.animWeights[i] - s.animWeights[i]) <= 0.5f / 255.0f + 1e-6f);
    CHECK(d.animFrame == doctest::Approx(43.5f));
    CHECK(d.flags == s.flags);
    CHECK(d.stageHash == s.stageHash);
    CHECK(d.scenario == 3);
}

TEST_CASE("decode rejects short, mistyped and non-finite messages") {
    PlayerState s, d;
    uint8_t wire[kPlayerStateSize], fwd[kPlayerStateForwardedSize];
    encodePlayerState(s, wire);
    forward(wire, 1, fwd);
    uint8_t id;
    CHECK_FALSE(decodeForwardedPlayerState(fwd, sizeof(fwd) - 1, &id, &d));
    fwd[0] = 0x11;
    CHECK_FALSE(decodeForwardedPlayerState(fwd, sizeof(fwd), &id, &d));
    s.pos[1] = INFINITY;
    encodePlayerState(s, wire);
    forward(wire, 1, fwd);
    CHECK_FALSE(decodeForwardedPlayerState(fwd, sizeof(fwd), &id, &d));
}

TEST_CASE("matrix <-> quaternion is consistent for arbitrary rotations") {
    srand(7);
    for (int n = 0; n < 200; n++) {
        float q[4];
        for (float& v : q) v = (float)rand() / RAND_MAX * 2.0f - 1.0f;
        float m[3][4] = {}, back[4], m2[3][4] = {};
        quatToMatrix(q, m);
        matrixToQuat(m, back);
        quatToMatrix(back, m2);
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) CHECK(m2[r][c] == doctest::Approx(m[r][c]).epsilon(1e-4));
    }
}

TEST_CASE("fuzz: random bytes never crash the decoder") {
    srand(11);
    uint8_t buf[80];
    PlayerState d;
    uint8_t id;
    for (int n = 0; n < 10000; n++) {
        size_t len = (size_t)(rand() % sizeof(buf));
        for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)rand();
        if (len) buf[0] = kMsgPlayerState;
        (void)decodeForwardedPlayerState(buf, len, &id, &d);
    }
    CHECK(true);
}

TEST_CASE("Contact round-trips") {
    Contact c;
    c.playerId = 3;
    c.kind = kContactTrample;
    c.dir[0] = 0.6f;
    c.dir[1] = -0.8f;
    c.dir[2] = 0.0f;
    uint8_t wire[kContactSize];
    encodeContact(c, wire);
    CHECK(wire[0] == kMsgContact);
    Contact d;
    REQUIRE(decodeContact(wire, sizeof(wire), &d));
    CHECK(d.playerId == 3);
    CHECK(d.kind == kContactTrample);
    for (int i = 0; i < 3; i++) CHECK(fabsf(d.dir[i] - c.dir[i]) <= halfStep(c.dir[i]));
    CHECK_FALSE(decodeContact(wire, sizeof(wire) - 1, &d));
    wire[0] = 0x10;
    CHECK_FALSE(decodeContact(wire, sizeof(wire), &d));
}
