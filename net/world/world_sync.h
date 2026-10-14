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
// LiveActor::makeActorAppeared: unplaced (spawned) actors, matched by name
// and position.
void port_world_actor_appeared(void* actor);
// Coin and star bit spawns check this: nonzero while a remote defeat runs
// the local copy's kill() (the defeater's drop event brings the loot).
int port_world_suppress_loot(void);
// The same for a drop from `source`: also while another player runs that
// actor here (actor sync: the owner's drop event brings the loot).
int port_world_suppress_loot_from(const void* source);
// MR::appearCoinPop / appearStarPiece: a local drop (kind 0 coins, 1 star
// bits), repeated on the other clients in this stage.
void port_world_loot_dropped(const void* source, int kind, float x, float y, float z, int count, float a, float b);
// Coin::noticeGetCoin (not purple coins), StarPiece::touchPlayer: picked up.
void port_world_item_collected(void* actor);
// The same, `purple`: a purple coin (counted for everyone).
void port_world_item_collected2(void* actor, int purple);
// RunawayRabbit::init: an actor whose catch is shared (kWorldCaught), so
// its death is no defeat.  RunawayRabbit::attackSensor: the local player
// caught it.
void port_world_catch_register(void* actor);
void port_world_caught(void* actor);
// RunawayRabbit.cpp: count a rabbit another player caught (0: it was caught
// here already).
int port_runaway_rabbit_caught_remote(void* rabbit);
// Not caught yet, and in a group none of whose rabbits is caught (debug).
int port_runaway_rabbit_catchable(void* rabbit);
// Coin.cpp: count a purple coin another player took.
void port_coin_count_purple(void);
// ChipGroup::noticeGet: the local player took chip `index` (registration
// order) of a star chip group.  Counted for everyone.
void port_world_chip_got(int chipType, int groupId, int index);
// ChipGroup.cpp: another player took it: gone and counted here; 0 if it was
// already.
int port_chip_take_remote(int chipType, int groupId, int index);
// Debug (mode 106): take the nearest star chip; 0 if none.
int port_chip_debug_take_nearest(float x, float y, float z);
// Coin / StarPiece::makeActorAppeared: items to match pickups against.
void port_world_item_appeared(void* actor);
// Scene init: the NameObj remote drops are made for (declared coins and
// star bits).
void port_world_declare_loot_host(const void* nameObj);
// Shooting stars (falling stars that burst into star bits) are led by the
// stage host (lowest player id in the stage and scenario): its stars fire on
// their own timer and announce it; everyone else's fire only when told, so
// the star bits rain down at the same moment everywhere.  `due`: the local
// timer says fire.  Returns whether to fire now.
int port_world_shooting_star_gate(void* actor, int due);
// A non-host star waiting for its next shot: told to fire already?
int port_world_shooting_star_signalled(void* actor);
// Around a drop every client makes by itself (a synced shooting star's
// burst): not announced.
void port_world_ambient_drop(int on);
// StageSwitchFunction::on/offSwitchBySwitchIdInfo: a switch changed
// (zone 0xFFFF: global).  Shared unless inside port_world_local_switch.
void port_world_switch_changed(int zone, int switchNo, int on);
// Around switches only the local player drives (areas he stands in).
void port_world_local_switch(int enter);
// MR::declareEventPowerStar: names event stars are requested by.
void port_world_event_star_declared(const char* name);
// MR::appearEventPowerStar: an event star appears (a boss beaten, a reward).
void port_world_event_star_appear(const char* name, int id, const void* pos, int continueDemo, int withoutDemo);
// PowerStar::init: the stars of the scene; PowerStar (Wait -> grab): the
// local player grabbed star `id`.
void port_world_power_star_register(void* star, int id);
void port_world_star_got(int id);
// PowerStar.cpp: pulls a waiting star onto the local player so he grabs it
// (1), 2 if it is being grabbed already, 0 if it cannot be yet.
int port_power_star_pull_to_player(void* star);
// Debug (mode 103): beat the nearest event star host (as a boss's defeat does).
void port_world_debug_event_star(float x, float y, float z);
// Debug (mode 108): the local player catches the nearest star bunny
// (without the catch demo: counted here and told to the room).
void port_world_debug_catch_nearest(float x, float y, float z);
// Debug (mode 105): the local player hits the nearest boss.
void port_world_debug_hit_boss(float x, float y, float z);
// Debug: the local player grabs the nearest waiting power star.
void port_world_debug_grab_star(void);
// Debug (F11): drop coins and star bits over (x, y, z).
void port_world_debug_drop(float x, float y, float z);
// HitSensor::receiveMessage, after the receiver answered: a hit the local
// player landed on a boss is replayed on the others' copy of the boss.
void port_world_hit(uint32_t msg, void* senderSensor, void* receiverSensor, int accepted);
// net/puppet: a puppet's body sensor, the sender of replayed hits.
void* port_net_puppet_sensor(void);
// Once per gameplay frame (net/puppet): apply received events.
void port_world_update(void);
// Actor sync: kill the local copy of an actor gone where its owner runs it
// (no loot: the owner's drop brings it).
void port_world_remote_kill(void* actor);
// A coin or star bit (pickups are synced as events).
int port_world_is_item(void* actor);
// Has an enemy hit sensor (what counts as a defeat).
int port_world_has_enemy_sensor(void* actor);
// Debug (F10): defeat the registered enemy nearest (x, y, z); 0 if none.
int port_world_debug_kill_nearest(float x, float y, float z);

#ifdef __cplusplus
}

namespace net {
constexpr uint8_t kMsgWorldEvent = 0x13;
// [0x13][u8 kind][u32 stage][u8 scenario][u32 placement id, or count]
// [u32 name hash][f32 pos[3]][f32 a][f32 b]
constexpr unsigned kWorldEventSize = 35;
enum : uint8_t {
    kWorldEnemyDefeated = 1,
    kWorldItemCollected = 2,    // a coin or star bit picked up
    kWorldCoinsDropped = 3,     // id = count
    kWorldStarBitsDropped = 4,  // id = count, a/b = launch speeds
    kWorldShootingStar = 5,     // a shooting star (id) fires now (stage host)
    kWorldSwitch = 6,           // id = zone << 16 | switch no (zone 0xFFFF: global), a = on
    kWorldEventStar = 7,        // an event star appears (a boss beaten...): name hash, id = star id, pos, a = flags
    kWorldStarGot = 8,          // a player grabbed power star `id`: everyone in the level gets it
    kWorldBossHit = 9,          // a hit on a boss: id = placement id or ordinal, pos = sender offset,
                                // a = msg | sensor index << 16, b = sender sensor type | placed << 16
    kWorldChipGot = 10,         // a star chip taken: id = type << 24 | index << 16 | group (u16)
    kWorldCaught = 11,          // a player caught a star bunny (Gateway's RunawayRabbit): id = placement id
};
// Placement id: FNV-1a of the name and the position rounded to whole units.
uint32_t worldPlacementId(const char* name, float x, float y, float z);
// A received WorldEvent (net_client, network thread of the game: queued).
void worldOnEvent(const uint8_t* data, unsigned len);
}  // namespace net
#endif
