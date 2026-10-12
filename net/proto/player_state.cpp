// PlayerState encoding (P2-01).  See the header for the layout.
#include "player_state.h"

#include <math.h>
#include <string.h>

namespace net {

// IEEE 754 binary16, round to nearest even; infinities and NaN preserved,
// values beyond the range saturate to infinity, subnormals kept.
uint16_t floatToHalf(float f) {
    uint32_t x;
    memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000;
    int32_t exp = (int32_t)((x >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = x & 0x7FFFFF;
    if (((x >> 23) & 0xFF) == 0xFF) {  // inf / nan
        return (uint16_t)(sign | 0x7C00 | (mant ? 0x200 : 0));
    }
    if (exp >= 31) {
        return (uint16_t)(sign | 0x7C00);
    }
    if (exp <= 0) {
        if (exp < -10) {
            return (uint16_t)sign;
        }
        mant |= 0x800000;
        uint32_t shift = (uint32_t)(14 - exp);
        uint32_t half = mant >> shift;
        uint32_t rem = mant & ((1u << shift) - 1);
        uint32_t mid = 1u << (shift - 1);
        if (rem > mid || (rem == mid && (half & 1))) half++;
        return (uint16_t)(sign | half);
    }
    uint32_t half = sign | ((uint32_t)exp << 10) | (mant >> 13);
    uint32_t rem = mant & 0x1FFF;
    if (rem > 0x1000 || (rem == 0x1000 && (half & 1))) half++;  // may carry into the exponent: correct
    return (uint16_t)half;
}

float halfToFloat(uint16_t h) {
    uint32_t sign = (uint32_t)(h & 0x8000) << 16;
    uint32_t exp = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x3FF;
    uint32_t x;
    if (exp == 0) {
        if (mant == 0) {
            x = sign;
        } else {  // subnormal: normalise
            int e = -1;
            do {
                mant <<= 1;
                e++;
            } while ((mant & 0x400) == 0);
            x = sign | ((uint32_t)(127 - 15 - e) << 23) | ((mant & 0x3FF) << 13);
        }
    } else if (exp == 31) {
        x = sign | 0x7F800000 | (mant << 13);
    } else {
        x = sign | ((exp - 15 + 127) << 23) | (mant << 13);
    }
    float f;
    memcpy(&f, &x, 4);
    return f;
}

namespace {

struct Writer {
    uint8_t* p;
    void u8(uint8_t v) { *p++ = v; }
    void u16(uint16_t v) {
        u8((uint8_t)v);
        u8((uint8_t)(v >> 8));
    }
    void u32(uint32_t v) {
        u16((uint16_t)v);
        u16((uint16_t)(v >> 16));
    }
    void f32(float f) {
        uint32_t v;
        memcpy(&v, &f, 4);
        u32(v);
    }
    void f16(float f) { u16(floatToHalf(f)); }
};

struct Reader {
    const uint8_t* p;
    uint8_t u8() { return *p++; }
    uint16_t u16() {
        uint16_t lo = u8();
        return (uint16_t)(lo | (u8() << 8));
    }
    uint32_t u32() {
        uint32_t lo = u16();
        return lo | ((uint32_t)u16() << 16);
    }
    float f32() {
        uint32_t v = u32();
        float f;
        memcpy(&f, &v, 4);
        return f;
    }
    float f16() { return halfToFloat(u16()); }
};

uint8_t weightToByte(float w) {
    if (!(w > 0.0f)) return 0;
    if (w >= 1.0f) return 255;
    return (uint8_t)lrintf(w * 255.0f);
}

}  // namespace

void encodePlayerState(const PlayerState& s, uint8_t* out) {
    Writer w{out};
    w.u8(kMsgPlayerState);
    w.u32(s.tick);
    for (float v : s.pos) w.f32(v);
    for (float v : s.rot) w.f16(v);
    for (float v : s.up) w.f16(v);
    for (float v : s.vel) w.f16(v);
    w.u32(s.animHash);
    for (float v : s.animWeights) w.u8(weightToByte(v));
    w.f16(s.animFrame);
    w.u16(s.flags);
    w.u32(s.stageHash);
    w.u8(s.scenario);
}

bool decodeForwardedPlayerState(const uint8_t* in, size_t len, uint8_t* senderId, PlayerState* s) {
    if (len < kPlayerStateForwardedSize || in[0] != kMsgPlayerState) {
        return false;
    }
    Reader r{in + 1};
    *senderId = r.u8();
    s->tick = r.u32();
    for (float& v : s->pos) v = r.f32();
    for (float& v : s->rot) v = r.f16();
    for (float& v : s->up) v = r.f16();
    for (float& v : s->vel) v = r.f16();
    s->animHash = r.u32();
    for (float& v : s->animWeights) v = r.u8() / 255.0f;
    s->animFrame = r.f16();
    s->flags = r.u16();
    s->stageHash = r.u32();
    s->scenario = r.u8();
    for (float v : s->pos) {
        if (!isfinite(v)) return false;
    }
    return true;
}

// Shepperd's method on the 3x3 part (m[row][col]).
void matrixToQuat(const float m[3][4], float q[4]) {
    float tr = m[0][0] + m[1][1] + m[2][2];
    if (tr > 0.0f) {
        float s = sqrtf(tr + 1.0f) * 2.0f;
        q[3] = 0.25f * s;
        q[0] = (m[2][1] - m[1][2]) / s;
        q[1] = (m[0][2] - m[2][0]) / s;
        q[2] = (m[1][0] - m[0][1]) / s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        float s = sqrtf(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
        q[3] = (m[2][1] - m[1][2]) / s;
        q[0] = 0.25f * s;
        q[1] = (m[0][1] + m[1][0]) / s;
        q[2] = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
        float s = sqrtf(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
        q[3] = (m[0][2] - m[2][0]) / s;
        q[0] = (m[0][1] + m[1][0]) / s;
        q[1] = 0.25f * s;
        q[2] = (m[1][2] + m[2][1]) / s;
    } else {
        float s = sqrtf(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
        q[3] = (m[1][0] - m[0][1]) / s;
        q[0] = (m[0][2] + m[2][0]) / s;
        q[1] = (m[1][2] + m[2][1]) / s;
        q[2] = 0.25f * s;
    }
    float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (n > 0.0f) {
        for (int i = 0; i < 4; i++) q[i] /= n;
    }
}

void quatToMatrix(const float qin[4], float m[3][4]) {
    float n = sqrtf(qin[0] * qin[0] + qin[1] * qin[1] + qin[2] * qin[2] + qin[3] * qin[3]);
    float x = 0, y = 0, z = 0, w = 1;
    if (n > 0.0f) {
        x = qin[0] / n, y = qin[1] / n, z = qin[2] / n, w = qin[3] / n;
    }
    m[0][0] = 1 - 2 * (y * y + z * z);
    m[0][1] = 2 * (x * y - z * w);
    m[0][2] = 2 * (x * z + y * w);
    m[1][0] = 2 * (x * y + z * w);
    m[1][1] = 1 - 2 * (x * x + z * z);
    m[1][2] = 2 * (y * z - x * w);
    m[2][0] = 2 * (x * z - y * w);
    m[2][1] = 2 * (y * z + x * w);
    m[2][2] = 1 - 2 * (x * x + y * y);
}

}  // namespace net
