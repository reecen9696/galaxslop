// The local player's sounds, replayed on their puppet on the other clients
// (jump and spin voices, footsteps, landings, the skid, a flip's whoosh...).
//
// Every positional sound the game plays for the local Mario starts on his
// actor's sound object (MarioActor::initSound): MR::startSound /
// startLevelSound and Mario::playSoundJ go through AudSoundObject::
// startSound / startLevelSound (+ startSoundParam / startLevelSoundParam for
// the ones with a volume or timing), and the animation's footsteps (.bas)
// through AudAnmSoundObject::startAnimSound.  The decomp calls the hooks
// below from there (TARGET_PC) with the final sound id: Luigi's and
// underwater variants are already picked.  System sounds (the HUD, combo
// chimes) are the local player's own and are not sent.
//
// The receiver plays the same id on the puppet's own sound object
// (LiveActor::initSound on the puppet's position), through the same calls,
// so the game's 3D sound places it: quieter with distance, panned to where
// the other player stands, cut off by the same voice limits.  The floor's
// map code (footstep material) comes along; a voice gets Luigi's pitch
// (AudSystem::switchToLuigi's port) from the puppet's character, not the
// local player's.
//
// PlayerSound 0x16 (docs/NETCODE.md), sent every second gameplay frame with
// anything queued: u8 type, u8 sender id, u8 count, then per sound u8 op
// (1 sound, 2 level sound, 3 animation sound, 4 stop), u32 sound id, u8 map
// code, u8 mask (1 params, 2 sound params), then for mask 1 i32 a, i32 b
// (startSoundParam's two; for a stop, a = fade frames), for mask 2 f32
// volume, fx mix, pitch, pan, dolby (an animation sound's, after
// AudAnmSoundObject::modifySoundParams).  A level sound (kept alive each
// frame it is called) is sent once per batch.
#pragma once

#include <stddef.h>
#include <stdint.h>

class LiveActor;

namespace net {

constexpr uint8_t kMsgPlayerSound = 0x16;

// net_client.cpp: a PlayerSound message arrived.
void sndOnMessage(const uint8_t* msg, size_t len);
// Each gameplay frame (port_net_puppets_update): notes the local player's
// sound object and sends what it played, every second frame.
void sndSend();
// Plays what player `remoteId` (0: the local player, for PETARI_LOOPBACK)
// played on `pPuppet`, `delayNs` after it arrived (the puppet is drawn that
// far behind).  With pPuppet null the queue is dropped.
void sndApply(uint8_t remoteId, LiveActor* pPuppet, bool luigi, int64_t delayNs);

}  // namespace net

extern "C" {
// Decomp hooks (TARGET_PC).  pObject is an AudSoundObject; anything but the
// local player's is ignored.
void port_net_snd_note(const void* pObject, uint32_t soundId, int op);
void port_net_snd_params(const void* pObject, uint32_t soundId, int32_t a, int32_t b);
void port_net_snd_anim(const void* pObject, uint32_t soundId, const float* pVolFxPitchPanDolby);
void port_net_snd_stop(const void* pObject, uint32_t soundId, uint32_t fade);
}
