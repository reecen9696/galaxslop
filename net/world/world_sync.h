// World sync, first slice (Phase 4): placed enemies defeated on one client
// die on the others in the same stage and scenario.
//
// Identity: an actor placed from the stage data registers in
// MR::initDefaultPos with an id hashed from its name and placed position,
// the same on every client running the same stage (ties broken by
// registration order).  When a registered enemy (an actor with an enemy hit
// sensor) dies (LiveActor::makeActorDead, alive -> dead), the client sends
// WorldEvent 0x13 (docs/NETCODE.md); receivers in the same stage and
// scenario kill their copy with its own kill().  No authority: whoever
// defeats it first wins.  Not yet: late joiners (an enemy defeated before
// you entered stays alive for you), enemies spawned by generators (not
// placed, so no id), respawns.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Game hooks (decomp, TARGET_PC).  `actor` is a LiveActor*, `iter` a
// const JMapInfoIter*, `name` the actor's name.
void port_world_register(void* actor, const char* name, float x, float y, float z);
void port_world_actor_dead(void* actor);
// A new scene: forget the old one's actors.
void port_world_scene_reset(void);
// Once per gameplay frame (net/puppet): apply received events.
void port_world_update(void);
// Debug (F10): defeat the registered enemy nearest (x, y, z); 0 if none.
int port_world_debug_kill_nearest(float x, float y, float z);

#ifdef __cplusplus
}

namespace net {
constexpr uint8_t kMsgWorldEvent = 0x13;
constexpr unsigned kWorldEventSize = 11;  // [0x13][u8 kind][u32 stage][u8 scenario][u32 id]
enum : uint8_t { kWorldEnemyDefeated = 1 };
// Placement id: FNV-1a of the name and the position rounded to whole units.
uint32_t worldPlacementId(const char* name, float x, float y, float z);
// A received WorldEvent (net_client, network thread of the game: queued).
void worldOnEvent(const uint8_t* data, unsigned len);
}  // namespace net
#endif
