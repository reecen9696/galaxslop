// Web build stand-ins for platform pieces not ported yet.
#include <stdint.h>

extern "C" {
// The Wii remote's speaker: audio_out.cpp mixes it on desktop; the web build
// has no audio output until P1-01 (the AI clock then runs on a timer).
void port_speaker_push(const int16_t*, int) {}
}
