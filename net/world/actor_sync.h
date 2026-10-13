// Actor state sync (P4-03): moving platforms, timed objects, carried items,
// enemies and NPCs look and move the same for everyone in a stage.
//
// Ownership: every placed actor has one owner among the players in the
// stage, the player nearest it, or the one standing on it, holding it or
// hitting it (a claim, held 1.5 s past the last touch).  The owner's game
// runs the actor as usual and sends its state (ActorState 0x14,
// docs/NETCODE.md): base matrix, nerve, hidden / collision / dead flags.
// The other games keep running their copy but show it where the owner's is
// (position and rotation extrapolated from the last two states) and, for map
// objects, follow its nerve (state machine), so timers and switches line up.
//
// Nerves are named across builds by `port_nerve_register` (NEW_NERVE, under
// TARGET_PC): "Class::Nerve" hashed, with the folder of the source file as
// the actor's family (map object, ride, enemy, boss, NPC).
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// NEW_NERVE (static initialisation): one nerve singleton and its name.
int port_nerve_register(const void* nerve, const char* name, const char* file);
// port_world_register: a placed actor with its placement id.
void port_actor_sync_track(void* actor, uint32_t id);
// MR::initDefaultPos: the placement's object name (what kind of actor).
void port_actor_sync_obj_name(void* actor, const char* objName);
// MapParts constructors: kind 1 MapPartsRailMover, 2 MapPartsRotator,
// 3 MapPartsRailRotator of `host` (its phase goes with the host's state).
void port_actor_sync_part(void* host, int kind, void* part);
// LiveActor::movement (before the sensors update): a copy run by another
// player takes the owner's position and nerve.
void port_actor_sync_movement(void* actor);
// Whether another player runs `actor` (so this copy shows theirs); `anim`,
// if given, gets the BCK they play ("" before any).  For an actor with its
// own behaviour to stand aside (BarneyNPC).
int port_actor_sync_driven(void* actor, const char** anim);
// LiveActor::calcAnmMtx (after calcAndSetBaseMtx): a copy run by another
// player is drawn (and collides) with the owner's matrix.
void port_actor_sync_mtx(void* actor);
// HitSensor::receiveMessage: `actor` got a message from `sender`'s host.
void port_actor_sync_message(void* actor, void* senderActor);
// LiveActor::makeActorDead.
void port_actor_sync_dead(void* actor);
// A boss, or a part of one (its nerves live in Game/Boss)?
int port_actor_is_boss(void* actor);
// Did the local player hold `actor` in the last few seconds (thrown)?
int port_actor_recently_held(void* actor);
// Coin and star bit drops: from an actor another player runs (its owner's
// drop event brings them)?
int port_actor_sync_remote_owned(const void* nameObj);
// Once per gameplay frame (port_world_update): ownership, sending.
void port_actor_sync_update(uint32_t stage, uint8_t scenario);
// A new scene.
void port_actor_sync_reset(void);

#ifdef __cplusplus
}

namespace net {
constexpr uint8_t kMsgActorState = 0x14;
// [0x14][u8 sender][u32 stage][u8 scenario][u32 sender frame][u8 count]
// then count x [u32 id][u32 nerve hash][u16 step][u8 flags][f32 pos[3]][i16 quat[4]]
constexpr unsigned kActorStateHeader = 12;
constexpr unsigned kActorStateEntry = 31;
void actorStateOnMessage(const uint8_t* data, unsigned len);
}  // namespace net
#endif
