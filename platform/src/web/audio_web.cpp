// Browser audio output (P1-01): the AI DMA stream goes into a ring in wasm
// memory that an AudioWorklet on the page reads directly (the memory is a
// SharedArrayBuffer) and resamples to the AudioContext's rate.  web/app.js
// creates the context on the first key press or click, as browsers require,
// and calls _web_audio_ring() to find the ring.  Until then
// port_audio_queued_ns() reports no device and the AI clock runs on a timer.
#include <emscripten.h>
#include <stdint.h>

#include <atomic>

#include "port/port.h"

namespace {

constexpr uint32_t kRingFrames = 1 << 14;  // ~340 ms at 48 kHz

// Layout shared with the worklet in web/app.js: five u32 then the samples.
struct Ring {
    std::atomic<uint32_t> write;      // frames written (game side)
    std::atomic<uint32_t> read;       // frames read (worklet)
    std::atomic<uint32_t> rate;       // source rate of the frames in the ring
    std::atomic<uint32_t> underruns;  // worklet found the ring empty
    std::atomic<uint32_t> attached;   // worklet running
    int16_t lr[kRingFrames * 2];      // interleaved left, right
};
Ring sRing;

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void* web_audio_ring(void) { return &sRing; }
EMSCRIPTEN_KEEPALIVE uint32_t web_audio_ring_frames(void) { return kRingFrames; }

// The AI stream is right sample first; the ring is left first.
void port_audio_submit(const int16_t* rightLeft, uint32_t frames, uint32_t rate) {
    sRing.rate.store(rate, std::memory_order_relaxed);
    if (!sRing.attached.load(std::memory_order_acquire)) {
        return;
    }
    uint32_t w = sRing.write.load(std::memory_order_relaxed);
    uint32_t space = kRingFrames - (w - sRing.read.load(std::memory_order_acquire));
    if (frames > space) {
        frames = space;  // the worklet stopped reading; the AI clock waits for it
    }
    for (uint32_t i = 0; i < frames; i++) {
        uint32_t idx = (w + i) & (kRingFrames - 1);
        sRing.lr[idx * 2] = rightLeft[i * 2 + 1];
        sRing.lr[idx * 2 + 1] = rightLeft[i * 2];
    }
    sRing.write.store(w + frames, std::memory_order_release);
}

int64_t port_audio_queued_ns(void) {
    if (!sRing.attached.load(std::memory_order_acquire)) {
        return -1;  // no AudioContext yet: timer pacing
    }
    uint32_t rate = sRing.rate.load(std::memory_order_relaxed);
    uint32_t queued = sRing.write.load(std::memory_order_relaxed) - sRing.read.load(std::memory_order_acquire);
    return rate ? (int64_t)queued * 1000000000ll / rate : 0;
}

int port_audio_underruns(void) { return (int)sRing.underruns.load(std::memory_order_relaxed); }

}  // extern "C"
