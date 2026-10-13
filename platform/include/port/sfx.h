// Sound effects of the port's own (assets/mods/Sound/*.wav, for characters
// the port adds), mixed into the game's audio output on top of everything
// the game plays.  WAV files: 16-bit PCM, mono or stereo, any rate.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Loads <mods>/Sound/<name>.wav once; returns its id, or -1.
int port_sfx_load(const char* name);
// Starts it from the beginning (one voice per sound: a replay restarts it).
void port_sfx_play(int id, float volume);

// Audio output side (audio_out.cpp, audio_web.cpp): adds the playing sounds
// to interleaved stereo frames at `rate`.
void port_sfx_mix(int16_t* lr, uint32_t frames, uint32_t rate);

#ifdef __cplusplus
}
#endif
