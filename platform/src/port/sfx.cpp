// See port/sfx.h.  Sounds are loaded on a game thread and mixed on the audio
// thread; a voice is a position into its sound, under one lock.
#include "port/sfx.h"

#include <stdio.h>
#include <string.h>

#include <mutex>
#include <string>
#include <vector>

#include "port/heap_routing.h"
#include "port/port.h"

extern "C" const char* port_dvd_mods_root(void);  // dvd.cpp

namespace {

struct Sound {
    std::string name;
    std::vector<int16_t> samples;  // interleaved, `channels` per frame
    uint32_t channels = 1;
    uint32_t rate = 32000;
    bool playing = false;
    double pos = 0.0;  // frames
    float volume = 1.0f;
};

std::mutex sLock;
std::vector<Sound> sSounds;

uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

bool readWav(const std::string& path, Sound* out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<uint8_t> d;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) d.insert(d.end(), buf, buf + n);
    fclose(f);
    if (d.size() < 12 || memcmp(d.data(), "RIFF", 4) != 0 || memcmp(d.data() + 8, "WAVE", 4) != 0) return false;
    bool fmt = false;
    for (size_t o = 12; o + 8 <= d.size();) {
        uint32_t size = le32(&d[o + 4]);
        const uint8_t* body = &d[o + 8];
        if (o + 8 + size > d.size()) break;
        if (memcmp(&d[o], "fmt ", 4) == 0 && size >= 16) {
            if (le16(body) != 1 || le16(body + 14) != 16) return false;  // 16-bit PCM only
            out->channels = le16(body + 2);
            out->rate = le32(body + 4);
            fmt = out->channels == 1 || out->channels == 2;
        } else if (memcmp(&d[o], "data", 4) == 0 && fmt) {
            out->samples.resize(size / 2);
            for (size_t i = 0; i < out->samples.size(); i++) out->samples[i] = (int16_t)le16(body + i * 2);
            return !out->samples.empty();
        }
        o += 8 + size + (size & 1);
    }
    return false;
}

}  // namespace

extern "C" int port_sfx_load(const char* name) {
    PortHostAllocScope host;
    std::lock_guard<std::mutex> lock(sLock);
    for (size_t i = 0; i < sSounds.size(); i++) {
        if (sSounds[i].name == name) return (int)i;
    }
    Sound s;
    s.name = name;
    std::string path = std::string(port_dvd_mods_root()) + "/Sound/" + name + ".wav";
    if (!readWav(path, &s)) {
        port_log("sfx: cannot load %s", path.c_str());
        return -1;
    }
    port_log("sfx: %s, %.2f s", name, (double)s.samples.size() / s.channels / s.rate);
    sSounds.push_back(std::move(s));
    return (int)sSounds.size() - 1;
}

extern "C" void port_sfx_play(int id, float volume) {
    std::lock_guard<std::mutex> lock(sLock);
    if (id < 0 || id >= (int)sSounds.size()) return;
    sSounds[id].playing = true;
    sSounds[id].pos = 0.0;
    sSounds[id].volume = volume;
}

extern "C" void port_sfx_mix(int16_t* lr, uint32_t frames, uint32_t rate) {
    std::lock_guard<std::mutex> lock(sLock);
    for (Sound& s : sSounds) {
        if (!s.playing) continue;
        double step = (double)s.rate / (double)rate;
        uint32_t total = (uint32_t)(s.samples.size() / s.channels);
        for (uint32_t i = 0; i < frames; i++) {
            uint32_t a = (uint32_t)s.pos;
            if (a + 1 >= total) {
                s.playing = false;
                break;
            }
            float t = (float)(s.pos - a);
            for (int c = 0; c < 2; c++) {
                uint32_t ch = s.channels == 2 ? (uint32_t)c : 0;
                float v0 = s.samples[a * s.channels + ch], v1 = s.samples[(a + 1) * s.channels + ch];
                int v = lr[i * 2 + c] + (int)((v0 + (v1 - v0) * t) * s.volume);
                lr[i * 2 + c] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
            }
            s.pos += step;
        }
    }
}
