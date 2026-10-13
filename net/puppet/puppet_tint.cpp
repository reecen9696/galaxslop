// Puppet clothes colours (P2-05).  See the header.
#include "puppet_tint.h"

#include <math.h>
#include <string.h>

namespace net {
namespace {

struct Paint {
    float hue;       // degrees
    float satScale;  // applied to the source saturation
    float valScale;  // applied to the source value (brightness)
};

// Indexed by tint; classic and auto are unused.  Values scales were tuned
// by eye against the textures: yellow, orange and pink need more brightness
// than red to read as themselves, white drops the saturation.
const Paint kPaints[kTintCount] = {
    {0, 1, 1},          {0, 1, 1},         {120, 1.0f, 0.95f}, {325, 0.6f, 1.35f}, {28, 1.0f, 1.25f},
    {280, 0.85f, 1.0f}, {190, 1.0f, 1.15f}, {52, 1.0f, 1.35f}, {0, 0.08f, 1.6f},   {0, 0.3f, 0.35f},
};

// The character's own colour: hue window (degrees from its centre),
// minimum saturation and value.  Mario's window stops short of the brown
// shoes (+20..+30) in the same texture.
struct Source {
    float hue, lo, hi;
};
const Source kMario = {0, -22, 12};
const Source kLuigi = {120, -20, 20};
const float kMinSat = 0.3f, kMinVal = 0.05f;

uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
void put16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

void toRgb(uint16_t c, float rgb[3]) {
    rgb[0] = ((c >> 11) & 31) / 31.0f;
    rgb[1] = ((c >> 5) & 63) / 63.0f;
    rgb[2] = (c & 31) / 31.0f;
}

uint16_t fromRgb(const float rgb[3]) {
    auto q = [](float v, int max) { return (uint16_t)lroundf(fminf(fmaxf(v, 0.0f), 1.0f) * max); };
    return (uint16_t)(q(rgb[0], 31) << 11 | q(rgb[1], 63) << 5 | q(rgb[2], 31));
}

void rgbToHsv(const float c[3], float* h, float* s, float* v) {
    float mx = fmaxf(c[0], fmaxf(c[1], c[2])), mn = fminf(c[0], fminf(c[1], c[2])), d = mx - mn;
    *v = mx;
    *s = mx > 0 ? d / mx : 0;
    if (d <= 0) {
        *h = 0;
    } else if (mx == c[0]) {
        *h = 60 * fmodf((c[1] - c[2]) / d + 6, 6);
    } else if (mx == c[1]) {
        *h = 60 * ((c[2] - c[0]) / d + 2);
    } else {
        *h = 60 * ((c[0] - c[1]) / d + 4);
    }
}

void hsvToRgb(float h, float s, float v, float c[3]) {
    float k[3] = {5, 3, 1};
    for (int i = 0; i < 3; i++) {
        float t = fmodf(k[i] + h / 60, 6);
        c[i] = v - v * s * fmaxf(0, fminf(fminf(t, 4 - t), 1));
    }
}

// The endpoint recoloured, or unchanged if it is not the character's colour.
uint16_t paint(uint16_t c, const Source& src, const Paint& p) {
    float rgb[3], h, s, v;
    toRgb(c, rgb);
    rgbToHsv(rgb, &h, &s, &v);
    float dh = fmodf(h - src.hue + 540, 360) - 180;
    if (dh < src.lo || dh > src.hi || s < kMinSat || v < kMinVal) {
        return c;
    }
    hsvToRgb(p.hue, fminf(1, s * p.satScale), fminf(1, v * p.valScale), rgb);
    return fromRgb(rgb);
}

// Swaps palette indices 0 and 1 in a block's four index bytes (2 bits per
// texel); with `andThreeTwo`, 2 and 3 as well.
void swapIndices(uint8_t* idx, bool andThreeTwo) {
    for (int y = 0; y < 4; y++) {
        uint8_t row = idx[y], out = 0;
        for (int x = 0; x < 4; x++) {
            int sh = 6 - 2 * x, i = (row >> sh) & 3;
            if (i < 2 || andThreeTwo) i ^= 1;
            out |= (uint8_t)(i << sh);
        }
        idx[y] = out;
    }
}

}  // namespace

uint8_t puppetTint(uint8_t requested, uint8_t playerId) {
    if (requested != kTintAuto && requested < kTintCount) {
        return requested;
    }
    if (playerId == 0) {
        return kTintClassic;
    }
    return (uint8_t)(kTintClassic + (playerId - 1) % (kTintCount - kTintClassic));
}

void tintCmpr(const uint8_t* src, uint8_t* dst, size_t bytes, bool luigi, uint8_t tint) {
    if (dst != src) {
        memmove(dst, src, bytes);
    }
    if (tint <= kTintClassic || tint >= kTintCount) {
        return;
    }
    const Source& from = luigi ? kLuigi : kMario;
    Paint to = kPaints[tint];
    if (luigi && tint == kTintGreen) {
        to.hue = 0;  // Luigi is green already: the "other colour" slot is red
    }
    // Each 8-byte block: two RGB565 endpoints, then 2-bit indices.  c0 > c1
    // means four colours; otherwise three and transparent.  Recolouring can
    // flip the order, so restore it (swapping the endpoints and remapping
    // the indices) to keep each block in its mode.
    for (size_t o = 0; o + 8 <= bytes; o += 8) {
        uint8_t* b = dst + o;
        uint16_t c0 = be16(b), c1 = be16(b + 2);
        bool four = c0 > c1;
        uint16_t n0 = paint(c0, from, to), n1 = paint(c1, from, to);
        if (n0 == c0 && n1 == c1) {
            continue;
        }
        if (four && n0 <= n1) {
            if (n0 == n1) {
                // Equal endpoints would switch to three colours + transparent.
                if (n1 > 0) {
                    n1--;
                } else {
                    n0++;
                }
            } else {
                uint16_t t = n0;
                n0 = n1;
                n1 = t;
                swapIndices(b + 4, true);
            }
        } else if (!four && n0 > n1) {
            uint16_t t = n0;
            n0 = n1;
            n1 = t;
            swapIndices(b + 4, false);
        }
        put16(b, n0);
        put16(b + 2, n1);
    }
}

}  // namespace net
