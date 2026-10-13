// The local player's particle effects, replayed on their puppet on the
// other clients (spin sparkles, landing and running smoke, damage, ...).
//
// Every effect the game plays on the local MarioActor goes through
// MR::emitEffect / deleteEffect / forceDeleteEffect (decomp
// EffectUtil.cpp calls port_net_fx_note), so those are captured there, by
// the name the game played it by and the emitter's index in the actor's
// EffectKeeper.  A puppet builds its EffectKeeper from the same "Mario"
// effect group in the same order (RemotePlayerPuppet::init), so the index
// names the same effect on every client even where MarioActor renamed it
// (MarioActor::initEffect), and registers MarioActor's common and floor-
// material effects (landing smoke rings, splashes; port_register_mario_
// common_effects in MarioEffect.cpp) under the same names, which the
// receiver tries first.  MarioActor's few situational extras (WaterColumnS,
// CrestGetFly...) the puppet lacks are skipped.  The world
// position, rotation and scale MarioActor::playEffectTrans / RT / RTZ / SRT
// give an emitter come along (port_net_fx_transform).
//
// PlayerEffect 0x15 (docs/NETCODE.md), sent once per frame with anything
// queued: u8 type, u8 sender id, u8 count, then per effect u8 op (1 play,
// 2 stop, 3 force stop), u16 index, u8 mask (1 translation, 2 rotation,
// 4 scale, 8 follow off), u8 name length, the name (Shift-JIS), then
// f32 t[3], f32 r[3] (degrees), f32 scale for the bits set.
#pragma once

#include <stddef.h>
#include <stdint.h>

class LiveActor;

namespace net {

constexpr uint8_t kMsgPlayerEffect = 0x15;

// net_client.cpp: a PlayerEffect message arrived.
void fxOnMessage(const uint8_t* msg, size_t len);
// Each gameplay frame (port_net_puppets_update): sends what the local
// player played this frame.
void fxSend();
// Plays what player `remoteId` (0: the local player, for PETARI_LOOPBACK)
// played on `pPuppet`, `delayNs` after it arrived (the puppet's pose is
// drawn that far behind).  With pPuppet null the queue is dropped.
void fxApply(uint8_t remoteId, LiveActor* pPuppet, int64_t delayNs);
// A puppet was hidden: its effects stop.
void fxStopAll(LiveActor* pPuppet);

}  // namespace net

extern "C" {
// Decomp hooks (TARGET_PC).
void port_net_fx_note(LiveActor* pActor, const char* pName, int op);
void port_net_fx_transform(LiveActor* pActor, const void* pEmitter, const float* pTrans, const float* pRotDeg, float scale, int followOff);
}
