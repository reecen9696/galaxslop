// World sync: defeated enemies, collected coins and star bits, dropped loot.
// See the header.
#include "world_sync.h"

#include "actor_sync.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/LiveActor/HitSensorInfo.hpp"
#include "Game/LiveActor/HitSensorKeeper.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Map/StageSwitch.hpp"
#include "Game/NPC/EventDirector.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "net/capture/player_capture.h"
#include "net/client/net_client.h"
#include "port/heap_routing.h"
#include "port/port.h"

namespace net {

namespace {
uint32_t fnv(uint32_t h, uint8_t b) { return (h ^ b) * 16777619u; }
uint32_t hashName(const char* name) {
    uint32_t h = 2166136261u;
    for (const char* p = name; p != nullptr && *p != 0; p++) h = fnv(h, (uint8_t)*p);
    return h;
}
}  // namespace

uint32_t worldPlacementId(const char* name, float x, float y, float z) {
    uint32_t h = hashName(name);
    const int32_t v[3] = {(int32_t)lroundf(x), (int32_t)lroundf(y), (int32_t)lroundf(z)};
    for (int32_t c : v) {
        for (int i = 0; i < 4; i++) h = fnv(h, (uint8_t)(c >> (8 * i)));
    }
    return h;
}

namespace {

// One WorldEvent.  Defeats and pickups: `id` is the placement id (0 for an
// actor spawned in play, which receivers match by name hash and position).
// Drops: `id` is the count, `a`/`b` the star bit launch speeds.
struct Event {
    uint8_t kind;
    uint32_t stage;
    uint8_t scenario;
    uint32_t id;
    uint32_t nameHash;
    float pos[3];
    float a, b;
};

struct World {
    std::unordered_map<void*, uint32_t> idOf;     // placed actor -> placement id
    std::unordered_map<uint32_t, void*> actorOf;  // placement id -> placed actor
    std::unordered_set<void*> unplaced;           // actors spawned during play (no placement id)
    std::unordered_set<void*> items;              // coins and star bits, placed or pooled
    std::unordered_set<void*> catches;            // caught rather than defeated (star bunnies)
    std::vector<Event> inbox;
    // This visit's defeats and pickups of placed actors (local or received),
    // told to players who arrive in the stage later.
    std::vector<Event> history;
    uint32_t historyStage = 0;
    uint8_t historyScenario = 0xFF;
    std::unordered_set<uint8_t> here;  // remote players already in this stage
    // Applying a remote event: a defeat's kill() drops no loot (the event's
    // sender drops it and says so); a remote drop is not sent back.
    bool applyingDefeat = false;
    bool applyingDrop = false;
    bool applyingPickup = false;
    // Gameplay of this scene has begun (first port_world_update).  Scene
    // set-up kills and hides actors too (boss parts, pooled objects).
    bool live = false;
    const void* lootHost = nullptr;     // declared source of remote drops
    const void* pendingHost = nullptr;  // to declare once placement starts
    std::unordered_set<void*> shootSignals;  // shooting stars told to fire
    bool ambientDrop = false;
    // Switches.
    int localSwitch = 0;     // inside a player-driven switch (areas)
    bool applyingSwitch = false;
    uint32_t frame = 0;
    std::unordered_map<uint32_t, std::pair<uint32_t, int>> switchRate;  // key -> (window start, changes)
    std::unordered_set<uint32_t> noisySwitches;                         // flip too often: kept local
    // Event stars and grabbing stars.
    std::unordered_map<uint32_t, std::string> starNames;  // name hash -> declared name
    std::unordered_set<uint32_t> appearedStars;           // name hash ^ id, appeared here
    bool applyingStar = false;
    std::vector<std::pair<void*, int>> powerStars;  // PowerStar, star id
    // Boss hits other players landed, delivered to the local boss when it can
    // take them (it may be in another phase here: tried for a while).
    struct PendingHit {
        LiveActor* actor;
        int sensor;
        uint32_t msg;
        uint32_t senderType;
        float offset[3];
        uint32_t until;
    };
    std::vector<PendingHit> hits;
    bool applyingHit = false;
    bool hasPendingGrab = false;
    int pendingGrab = 0;  // star id the others grabbed (-1: the scenario's star)
    uint32_t pendingGrabUntil = 0;
    bool grabbedHere = false;
};

World& world() {
    static World* w = [] {
        PortHostAllocScope host;
        return new World();
    }();
    return *w;
}

bool hasEnemySensor(LiveActor* actor) {
    // Collectibles that register an enemy sensor (ChipBase: blue and yellow
    // star chips, "...チップ") are not defeats (kWorldChipGot syncs them).
    if (actor->mName != nullptr && strstr(actor->mName, "\x83\x60\x83\x62\x83\x76") != nullptr) {
        return false;
    }
    HitSensorKeeper* keeper = actor->mSensorKeeper;
    if (keeper == nullptr) {
        return false;
    }
    for (int i = 0; i < keeper->mSensorCount; i++) {
        HitSensorInfo* info = keeper->getNthSensorInfo(i);
        if (info != nullptr && info->mSensor != nullptr && MR::isSensorEnemy(info->mSensor)) {
            return true;
        }
    }
    return false;
}

void put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}
uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
void putF(uint8_t* p, float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    put32(p, u);
}
float getF(const uint8_t* p) {
    uint32_t u = get32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

bool enabled() {
    static const char* s = getenv("SMG_WORLD_SYNC");  // "0": off
    return net::netEnabled() && !(s != nullptr && s[0] == '0');
}

bool logging() {
    static const bool s = getenv("PETARI_WORLDLOG") != nullptr;
    return s;
}

void send(const Event& e) {
    uint8_t m[kWorldEventSize];
    m[0] = kMsgWorldEvent;
    m[1] = e.kind;
    put32(m + 2, e.stage);
    m[6] = e.scenario;
    put32(m + 7, e.id);
    put32(m + 11, e.nameHash);
    for (int i = 0; i < 3; i++) putF(m + 15 + 4 * i, e.pos[i]);
    putF(m + 27, e.a);
    putF(m + 31, e.b);
    netSendRaw(m, sizeof(m));
}

// Where the local player is now; false outside gameplay.
bool currentStage(uint32_t* pStage, uint8_t* pScenario) {
    NetPlayerCapture now;
    if (!net_capture_latest(&now)) {
        return false;
    }
    *pStage = now.stageHash;
    *pScenario = (uint8_t)now.scenario;
    return true;
}

// The visit's history belongs to one stage and scenario; only placed actors
// (stable ids) are worth repeating to an arrival.
void remember(const Event& e) {
    World& w = world();
    bool byId = e.kind == kWorldEnemyDefeated || e.kind == kWorldItemCollected || e.kind == kWorldCaught;
    if ((byId && e.id == 0) || (!byId && e.kind != kWorldSwitch && e.kind != kWorldEventStar && e.kind != kWorldChipGot)) {
        return;
    }
    if (e.stage != w.historyStage || e.scenario != w.historyScenario) {
        w.history.clear();
        w.historyStage = e.stage;
        w.historyScenario = e.scenario;
    }
    if (e.kind == kWorldSwitch) {
        // A switch's latest state only.
        for (size_t i = 0; i < w.history.size(); i++) {
            if (w.history[i].kind == kWorldSwitch && w.history[i].id == e.id) {
                w.history.erase(w.history.begin() + (long)i);
                break;
            }
        }
    }
    w.history.push_back(e);
}

// The nearest live actor of `set` named like `nameHash` within `radius`.
LiveActor* nearest(const std::unordered_set<void*>& set, uint32_t nameHash, const float pos[3], float radius) {
    LiveActor* best = nullptr;
    float bestD = radius * radius;
    for (void* p : set) {
        LiveActor* a = static_cast< LiveActor* >(p);
        if (MR::isDead(a) || hashName(a->mName) != nameHash) {
            continue;
        }
        float dx = a->mPosition.x - pos[0], dy = a->mPosition.y - pos[1], dz = a->mPosition.z - pos[2];
        float dist = dx * dx + dy * dy + dz * dz;
        if (dist < bestD) {
            bestD = dist;
            best = a;
        }
    }
    return best;
}

// The local copy of a defeated enemy or collected item: by placement id,
// else the nearest live one of the same name (enemies within 400 units,
// items, which fly about after a drop, within 300).
LiveActor* findLocal(const Event& e) {
    World& w = world();
    if (e.id != 0) {
        auto it = w.actorOf.find(e.id);
        return it != w.actorOf.end() ? static_cast< LiveActor* >(it->second) : nullptr;
    }
    if (e.kind == kWorldItemCollected) {
        return nearest(w.items, e.nameHash, e.pos, 300.0f);
    }
    return nearest(w.unplaced, e.nameHash, e.pos, 400.0f);
}

// A defeat as the other player saw it: the enemy's own death effect and the
// standard defeat sound, then its own kill() (cleanup, parts), its loot
// suppressed (the defeating player's drop event brings it).
void defeatLocally(LiveActor* actor) {
    World& w = world();
    if (MR::isRegisteredEffect(actor, "Death")) {
        MR::emitEffect(actor, "Death");
    }
    MR::startSound(actor, "SE_EM_EXPLODE_S", -1, -1);
    w.applyingDefeat = true;
    actor->kill();
    w.applyingDefeat = false;
}

// Another player took it: it vanishes here (its own kill(): a coin just
// goes, a star bit breaks), nothing is added to the local counters.
void collectLocally(LiveActor* actor) {
    World& w = world();
    w.applyingPickup = true;
    actor->kill();
    w.applyingPickup = false;
}

void dropLocally(const Event& e) {
    World& w = world();
    if (w.lootHost == nullptr) {
        return;
    }
    TVec3f pos(e.pos[0], e.pos[1], e.pos[2]);
    w.applyingDrop = true;
    if (e.kind == kWorldCoinsDropped) {
        MR::appearCoinPop(static_cast< const NameObj* >(w.lootHost), pos, (s32)e.id);
    } else {
        MR::appearStarPiece(static_cast< const NameObj* >(w.lootHost), pos, (s32)e.id, e.a, e.b, false);
    }
    w.applyingDrop = false;
}

}  // namespace

void worldOnEvent(const uint8_t* data, unsigned len) {
    if (len != kWorldEventSize || data[0] != kMsgWorldEvent) {
        return;
    }
    PortHostAllocScope host;
    Event e;
    e.kind = data[1];
    e.stage = get32(data + 2);
    e.scenario = data[6];
    e.id = get32(data + 7);
    e.nameHash = get32(data + 11);
    for (int i = 0; i < 3; i++) e.pos[i] = getF(data + 15 + 4 * i);
    e.a = getF(data + 27);
    e.b = getF(data + 31);
    world().inbox.push_back(e);
}

}  // namespace net

using namespace net;

extern "C" void port_world_register(void* actor, const char* name, float x, float y, float z) {
    if (!enabled()) {
        return;
    }
    World& w = world();
    if (w.pendingHost != nullptr) {
        // Placement has begun: the star bit director exists, and star bits
        // are allocated for all declarations after placement
        // (GameScene::init -> MR::createStarPiece).  Declare now.
        const NameObj* host = static_cast< const NameObj* >(w.pendingHost);
        w.pendingHost = nullptr;
        MR::declareCoin(host, 300);
        MR::declareStarPiece(host, 600);
        w.lootHost = host;
    }
    PortHostAllocScope host;
    uint32_t id = worldPlacementId(name, x, y, z);
    // Same name at the same spot: the next ids by registration order, which
    // follows the stage data and so matches on every client.
    for (uint32_t salt = 1; (id == 0 || w.actorOf.count(id) != 0) && salt < 64; salt++) {
        id = worldPlacementId(name, x, y, z) + salt * 0x9E3779B9u;
    }
    w.idOf[actor] = id;
    w.actorOf[id] = actor;
    port_actor_sync_track(actor, id);
}

extern "C" void port_world_actor_appeared(void* actor) {
    if (!enabled()) {
        return;
    }
    World& w = world();
    // Only actors spawned during gameplay count (generator enemies); ones
    // that appear while the scene is set up (boss parts, pools) do not.
    if (w.live && w.idOf.count(actor) == 0 && w.unplaced.count(actor) == 0) {
        PortHostAllocScope host;
        w.unplaced.insert(actor);
    }
}

extern "C" void port_world_item_appeared(void* actor) {
    if (!enabled()) {
        return;
    }
    World& w = world();
    if (w.items.count(actor) == 0) {
        PortHostAllocScope host;
        w.items.insert(actor);
    }
}

extern "C" void port_world_declare_loot_host(const void* nameObj) {
    if (!enabled()) {
        return;
    }
    // Room for the drops of the whole visit (each drop counts against it),
    // declared when placement starts (port_world_register): the star bit
    // director does not exist yet while puppets are created.
    world().pendingHost = nameObj;
}

extern "C" void port_world_scene_reset(void) {
    PortHostAllocScope host;
    World& w = world();
    w.idOf.clear();
    w.actorOf.clear();
    w.unplaced.clear();
    w.items.clear();
    w.catches.clear();
    w.inbox.clear();
    w.history.clear();
    w.historyScenario = 0xFF;
    w.here.clear();
    w.live = false;
    w.lootHost = nullptr;
    w.pendingHost = nullptr;
    w.shootSignals.clear();
    w.ambientDrop = false;
    w.localSwitch = 0;
    w.switchRate.clear();
    w.noisySwitches.clear();
    w.starNames.clear();
    w.appearedStars.clear();
    w.powerStars.clear();
    w.hasPendingGrab = false;
    w.grabbedHere = false;
    w.hits.clear();
    port_actor_sync_reset();
}

namespace {
// The lowest player id among the local player and the others in this stage
// and scenario leads shared timers.  Alone (or offline): yes.
bool isStageHost() {
    if (!enabled() || netLocalId() == 0) {
        return true;
    }
    uint32_t stage;
    uint8_t scenario;
    if (!currentStage(&stage, &scenario)) {
        return true;
    }
    for (int i = 0; i < kMaxRemotes; i++) {
        const RemoteView& v = netRemote(i);
        if (v.present && v.id < netLocalId() && v.pose.stageHash == stage && (uint8_t)v.pose.scenario == scenario) {
            return false;
        }
    }
    return true;
}
}  // namespace

extern "C" int port_world_shooting_star_gate(void* actor, int due) {
    World& w = world();
    if (isStageHost()) {
        if (due && enabled()) {
            auto it = w.idOf.find(actor);
            Event e{};
            if (it != w.idOf.end() && currentStage(&e.stage, &e.scenario)) {
                e.kind = kWorldShootingStar;
                e.id = it->second;
                PortHostAllocScope host;
                send(e);
                if (logging()) port_log("world: shooting star %08x fires (host)", e.id);
            }
        }
        return due;
    }
    // Not the host: only when told.
    auto it = w.shootSignals.find(actor);
    if (it == w.shootSignals.end()) {
        return 0;
    }
    PortHostAllocScope host;
    w.shootSignals.erase(it);
    if (logging()) port_log("world: shooting star fires (told by the host)");
    return 1;
}

extern "C" int port_world_shooting_star_signalled(void* actor) {
    World& w = world();
    return !isStageHost() && w.shootSignals.count(actor) != 0 ? 1 : 0;
}

extern "C" void port_world_ambient_drop(int on) { world().ambientDrop = on != 0; }

extern "C" int port_world_suppress_loot(void) {
    if (!world().applyingDefeat) {
        return 0;
    }
    if (logging()) port_log("world: loot from a remote defeat suppressed (the defeater's drop event brings it)");
    return 1;
}

extern "C" int port_world_suppress_loot_from(const void* source) {
    if (port_world_suppress_loot()) {
        return 1;
    }
    if (world().live && port_actor_sync_remote_owned(source)) {
        if (logging()) port_log("world: loot from %s suppressed (another player runs it)", static_cast< const NameObj* >(source)->mName);
        return 1;
    }
    return 0;
}

extern "C" void port_world_remote_kill(void* actor) {
    World& w = world();
    w.applyingDefeat = true;
    static_cast< LiveActor* >(actor)->kill();
    w.applyingDefeat = false;
}

extern "C" int port_world_is_item(void* actor) { return world().items.count(actor) != 0 ? 1 : 0; }

extern "C" int port_world_has_enemy_sensor(void* actor) { return hasEnemySensor(static_cast< LiveActor* >(actor)) ? 1 : 0; }

extern "C" void port_world_loot_dropped(const void* source, int kind, float x, float y, float z, int count, float a, float b) {
    World& w = world();
    if (!enabled() || !w.live || w.applyingDrop || w.applyingDefeat || w.ambientDrop || count <= 0) {
        return;
    }
    Event e{};
    if (!currentStage(&e.stage, &e.scenario)) {
        return;
    }
    e.kind = kind == 0 ? kWorldCoinsDropped : kWorldStarBitsDropped;
    e.id = (uint32_t)count;
    e.pos[0] = x;
    e.pos[1] = y;
    e.pos[2] = z;
    e.a = a;
    e.b = b;
    PortHostAllocScope host;
    send(e);
    if (logging())
        port_log("world: dropped %d %s at %.0f %.0f %.0f (from %s), told the room", count, kind == 0 ? "coins" : "star bits", x, y, z,
                 source ? static_cast< const NameObj* >(source)->mName : "?");
}

extern "C" void port_world_item_collected(void* actorPtr) { port_world_item_collected2(actorPtr, 0); }

extern "C" void port_world_item_collected2(void* actorPtr, int purple) {
    LiveActor* actor = static_cast< LiveActor* >(actorPtr);
    World& w = world();
    if (!enabled() || !w.live || w.applyingPickup) {
        return;
    }
    Event e{};
    if (!currentStage(&e.stage, &e.scenario)) {
        return;
    }
    auto it = w.idOf.find(actorPtr);
    e.kind = kWorldItemCollected;
    e.id = it != w.idOf.end() ? it->second : 0;
    e.a = purple ? 1.0f : 0.0f;
    e.nameHash = hashName(actor->mName);
    e.pos[0] = actor->mPosition.x;
    e.pos[1] = actor->mPosition.y;
    e.pos[2] = actor->mPosition.z;
    PortHostAllocScope host;
    send(e);
    remember(e);
    if (logging()) port_log("world: collected %s (%s %08x), told the room", actor->mName, e.id ? "id" : "spawned", e.id);
}

extern "C" void port_world_chip_got(int chipType, int groupId, int index) {
    World& w = world();
    if (!enabled() || !w.live) {
        return;
    }
    Event e{};
    if (!currentStage(&e.stage, &e.scenario)) {
        return;
    }
    e.kind = kWorldChipGot;
    e.id = (uint32_t)(chipType & 0xFF) << 24 | (uint32_t)(index & 0xFF) << 16 | ((uint32_t)groupId & 0xFFFFu);
    PortHostAllocScope host;
    send(e);
    remember(e);
    if (logging()) port_log("world: took %s chip %d of group %d, told the room", chipType == 0 ? "blue" : "yellow", index, groupId);
}

extern "C" void port_world_actor_dead(void* actorPtr) {
    LiveActor* actor = static_cast< LiveActor* >(actorPtr);
    World& w = world();
    if (!enabled() || !w.live || w.applyingDefeat || w.applyingPickup || MR::isDead(actor)) {
        return;  // scene set-up, a remote event, or already dead
    }
    auto it = w.idOf.find(actorPtr);
    bool placed = it != w.idOf.end();
    if ((!placed && w.unplaced.count(actorPtr) == 0) || w.items.count(actorPtr) != 0 || w.catches.count(actorPtr) != 0 ||
        !hasEnemySensor(actor)) {
        return;  // a caught star bunny goes as kWorldCaught instead
    }
    Event e{};
    if (!currentStage(&e.stage, &e.scenario)) {
        return;
    }
    e.kind = kWorldEnemyDefeated;
    e.id = placed ? it->second : 0;
    e.nameHash = hashName(actor->mName);
    e.pos[0] = actor->mPosition.x;
    e.pos[1] = actor->mPosition.y;
    e.pos[2] = actor->mPosition.z;
    PortHostAllocScope host;
    send(e);
    remember(e);
    if (logging()) port_log("world: defeated %s (%s %08x), told the room", actor->mName, placed ? "id" : "spawned", e.id);
}

extern "C" void port_world_catch_register(void* actor) {
    if (!enabled() || actor == nullptr) {
        return;
    }
    PortHostAllocScope host;
    world().catches.insert(actor);
}

extern "C" void port_world_caught(void* actorPtr) {
    LiveActor* actor = static_cast< LiveActor* >(actorPtr);
    World& w = world();
    if (!enabled() || !w.live) {
        return;
    }
    auto it = w.idOf.find(actorPtr);
    Event e{};
    if (it == w.idOf.end() || !currentStage(&e.stage, &e.scenario)) {
        return;
    }
    e.kind = kWorldCaught;
    e.id = it->second;
    e.nameHash = hashName(actor->mName);
    e.pos[0] = actor->mPosition.x;
    e.pos[1] = actor->mPosition.y;
    e.pos[2] = actor->mPosition.z;
    PortHostAllocScope host;
    send(e);
    remember(e);
    if (logging()) port_log("world: caught %s (%08x), told the room", actor->mName, e.id);
}

extern "C" void port_world_debug_catch_nearest(float x, float y, float z) {
    World& w = world();
    LiveActor* best = nullptr;
    float bestD = 1e30f;
    for (void* p : w.catches) {
        LiveActor* a = static_cast< LiveActor* >(p);
        float dx = a->mPosition.x - x, dy = a->mPosition.y - y, dz = a->mPosition.z - z;
        float d = dx * dx + dy * dy + dz * dz;
        if (d < bestD && port_runaway_rabbit_catchable(p)) {
            bestD = d;
            best = a;
        }
    }
    if (best == nullptr) {
        port_log("world: debug: no star bunny to catch");
        return;
    }
    port_log("world: debug: catching %s at %.0f %.0f %.0f", best->mName, best->mPosition.x, best->mPosition.y, best->mPosition.z);
    if (port_runaway_rabbit_caught_remote(best)) {
        port_world_caught(best);
    }
}

// ---- Switches ----

namespace {
void applySwitch(const Event& e) {
    if (!MR::isExistSceneObj(SceneObj_StageSwitchContainer)) {
        return;
    }
    StageSwitchContainer* c = MR::getSceneObj< StageSwitchContainer >(SceneObj_StageSwitchContainer);
    int zone = (int)(e.id >> 16), no = (int)(e.id & 0xFFFF);
    ZoneSwitch* z = zone == 0xFFFF ? c->getGlobalSwitches() : c->getZoneSwitches(zone);
    bool on = e.a != 0.0f;
    if (z == nullptr || no >= 128 || z->get(no) == on) {
        return;
    }
    if (logging()) port_log("world: switch %d/%d %s by another player", zone, no, on ? "on" : "off");
    z->set(no, on);  // watchers (doors, bridges, appearing objects) react next frame
}

// The event star of `name` and `id` appeared here (or is about to).
uint32_t starKey(uint32_t nameHash, int32_t id) { return nameHash ^ ((uint32_t)id * 0x9E3779B9u); }
}  // namespace

extern "C" void port_world_local_switch(int enter) { world().localSwitch += enter ? 1 : -1; }

extern "C" void port_world_switch_changed(int zone, int switchNo, int on) {
    World& w = world();
    if (!enabled() || !w.live || w.applyingSwitch || w.localSwitch > 0) {
        return;
    }
    Event e{};
    if (!currentStage(&e.stage, &e.scenario)) {
        return;
    }
    e.kind = kWorldSwitch;
    e.id = (uint32_t)(zone & 0xFFFF) << 16 | (uint32_t)(switchNo & 0xFFFF);
    e.a = on ? 1.0f : 0.0f;
    PortHostAllocScope host;
    // A switch that flips all the time (a timer, a cycle) is the same on
    // every client already, or not worth the traffic: kept local.
    if (w.noisySwitches.count(e.id) != 0) {
        return;
    }
    auto& r = w.switchRate[e.id];
    if (w.frame - r.first > 120) {
        r = {w.frame, 0};
    }
    if (++r.second > 6) {
        w.noisySwitches.insert(e.id);
        if (logging()) port_log("world: switch %d/%d flips too often, kept local", zone, switchNo);
        return;
    }
    send(e);
    remember(e);
    if (logging()) port_log("world: switch %d/%d %s, told the room", zone, switchNo, on ? "on" : "off");
}

// ---- Event stars (a boss beaten, a reward) and grabbing a star ----

extern "C" void port_world_event_star_declared(const char* name) {
    if (!enabled() || name == nullptr) {
        return;
    }
    PortHostAllocScope host;
    world().starNames[hashName(name)] = name;
}

extern "C" void port_world_event_star_appear(const char* name, int id, const void* pos, int continueDemo, int withoutDemo) {
    World& w = world();
    if (!enabled() || name == nullptr) {
        return;
    }
    PortHostAllocScope host;
    uint32_t h = hashName(name);
    w.starNames[h] = name;
    bool fresh = w.appearedStars.insert(starKey(h, id)).second;
    if (w.applyingStar || !w.live || !fresh) {
        return;
    }
    Event e{};
    if (!currentStage(&e.stage, &e.scenario)) {
        return;
    }
    e.kind = kWorldEventStar;
    e.id = (uint32_t)id;
    e.nameHash = h;
    if (pos != nullptr) {
        const TVec3f* v = static_cast< const TVec3f* >(pos);
        e.pos[0] = v->x;
        e.pos[1] = v->y;
        e.pos[2] = v->z;
    }
    e.a = (float)((pos != nullptr ? 1 : 0) | (continueDemo ? 2 : 0) | (withoutDemo ? 4 : 0));
    send(e);
    remember(e);
    if (logging()) port_log("world: event star of %s (%d) appears, told the room", name, id);
}

namespace {
// Another player beat what this event star rewards (a boss): the boss is
// beaten here too (its own kill(), as for any defeated enemy), and the star
// appears the way it did for them.
void applyEventStar(const Event& e) {
    World& w = world();
    auto n = w.starNames.find(e.nameHash);
    if (n == w.starNames.end() || w.appearedStars.count(starKey(e.nameHash, (int32_t)e.id)) != 0) {
        return;  // not declared here, or it appeared here already
    }
    std::string name = n->second;
    LiveActor* boss = nullptr;
    for (auto& kv : w.idOf) {
        LiveActor* a = static_cast< LiveActor* >(kv.first);
        if (!MR::isDead(a) && hashName(a->mName) == e.nameHash && hasEnemySensor(a)) {
            boss = a;
            break;
        }
    }
    if (boss == nullptr) {
        for (void* p : w.unplaced) {
            LiveActor* a = static_cast< LiveActor* >(p);
            if (!MR::isDead(a) && hashName(a->mName) == e.nameHash && hasEnemySensor(a)) {
                boss = a;
                break;
            }
        }
    }
    if (logging()) port_log("world: %s beaten by another player, its star appears%s", name.c_str(), boss ? " (defeated here)" : "");
    w.applyingStar = true;
    if (boss != nullptr) {
        defeatLocally(boss);
    }
    if (w.appearedStars.insert(starKey(e.nameHash, (int32_t)e.id)).second) {
        TVec3f pos(e.pos[0], e.pos[1], e.pos[2]);
        int flags = (int)e.a;
        MR::appearEventPowerStar(name.c_str(), (s32)e.id, (flags & 1) ? &pos : nullptr, (flags & 2) != 0, (flags & 4) != 0);
    }
    w.applyingStar = false;
}

// The star another player grabbed comes to the local player, who grabs it
// too: both see the star-get and leave the level together.
void updatePendingGrab() {
    World& w = world();
    if (!w.hasPendingGrab) {
        return;
    }
    if (w.grabbedHere || w.frame > w.pendingGrabUntil) {
        if (!w.grabbedHere && logging()) port_log("world: star %d never became grabbable here, gave up", w.pendingGrab);
        w.hasPendingGrab = false;
        return;
    }
    for (auto& s : w.powerStars) {
        if (s.second != w.pendingGrab) {
            continue;
        }
        int r = port_power_star_pull_to_player(s.first);
        if (r == 2) {
            w.hasPendingGrab = false;  // being grabbed
            return;
        }
    }
}
}  // namespace

extern "C" void port_world_power_star_register(void* star, int id) {
    if (!enabled()) {
        return;
    }
    PortHostAllocScope host;
    world().powerStars.push_back({star, id});
}

extern "C" void port_world_star_got(int id) {
    World& w = world();
    if (!enabled()) {
        return;
    }
    bool first = !w.grabbedHere;
    w.grabbedHere = true;
    Event e{};
    if (!first || !currentStage(&e.stage, &e.scenario)) {
        return;
    }
    e.kind = kWorldStarGot;
    e.id = (uint32_t)id;
    PortHostAllocScope host;
    send(e);
    if (logging()) port_log("world: grabbed power star %d, the others in the level get it too", id);
}

// ---- Boss hits ----

namespace {
int sensorIndex(LiveActor* a, HitSensor* sensor) {
    HitSensorKeeper* k = a->mSensorKeeper;
    for (int i = 0; k != nullptr && i < k->mSensorCount; i++) {
        HitSensorInfo* info = k->getNthSensorInfo(i);
        if (info != nullptr && info->mSensor == sensor) return i;
    }
    return -1;
}

// Actors made at scene set-up (boss parts) have no placement id: the n-th
// actor of their name, in creation order (the same on every client).
uint32_t ordinalOf(LiveActor* a) {
    AllLiveActorGroup* all = MR::getAllLiveActorGroup();
    uint32_t h = hashName(a->mName), n = 0;
    for (int i = 0; i < all->getObjNum(); i++) {
        LiveActor* o = all->getActor(i);
        if (o == a) return n;
        if (o != nullptr && hashName(o->mName) == h) n++;
    }
    return 0xFFFFFFFFu;
}

LiveActor* byOrdinal(uint32_t nameHash, uint32_t ordinal) {
    AllLiveActorGroup* all = MR::getAllLiveActorGroup();
    uint32_t n = 0;
    for (int i = 0; i < all->getObjNum(); i++) {
        LiveActor* o = all->getActor(i);
        if (o != nullptr && hashName(o->mName) == nameHash && n++ == ordinal) return o;
    }
    return nullptr;
}

bool deliverHit(const World::PendingHit& h) {
    World& w = world();
    HitSensor* sender = static_cast< HitSensor* >(port_net_puppet_sensor());
    HitSensorKeeper* k = h.actor->mSensorKeeper;
    if (sender == nullptr || k == nullptr || h.sensor >= k->mSensorCount || MR::isDead(h.actor)) return false;
    HitSensorInfo* info = k->getNthSensorInfo(h.sensor);
    if (info == nullptr || info->mSensor == nullptr) return false;
    HitSensor* receiver = info->mSensor;
    // The puppet's sensor stands in for the attacker: where it was relative
    // to the part it hit, and of its kind (the player, a thrown shell...).
    TVec3f savedPos = sender->mPosition;
    u32 savedType = sender->mType;
    sender->mPosition.set(receiver->mPosition.x + h.offset[0], receiver->mPosition.y + h.offset[1], receiver->mPosition.z + h.offset[2]);
    sender->mType = h.senderType;
    w.applyingHit = true;
    bool ok = receiver->receiveMessage(h.msg, sender);
    w.applyingHit = false;
    sender->mPosition = savedPos;
    sender->mType = savedType;
    return ok;
}

void updateHits() {
    World& w = world();
    for (size_t i = 0; i < w.hits.size();) {
        World::PendingHit& h = w.hits[i];
        bool done = deliverHit(h);
        if (done || w.frame > h.until || MR::isDead(h.actor)) {
            if (logging()) port_log("world: boss hit on %s %s", h.actor->mName, done ? "taken here too" : "never taken here (gave up)");
            w.hits.erase(w.hits.begin() + (long)i);
        } else {
            i++;
        }
    }
}
}  // namespace

extern "C" void port_world_hit(uint32_t msg, void* senderPtr, void* receiverPtr, int accepted) {
    World& w = world();
    if (!accepted || !enabled() || !w.live || w.applyingHit || senderPtr == nullptr) {
        return;
    }
    HitSensor* sender = static_cast< HitSensor* >(senderPtr);
    HitSensor* receiver = static_cast< HitSensor* >(receiverPtr);
    LiveActor* boss = receiver->mHost;
    if (boss == nullptr || !port_actor_is_boss(boss)) {
        return;
    }
    // Hits of the local player: his own attacks, star bits he shot, what he
    // threw.
    bool byPlayer = (void*)sender->mHost == (void*)MarioAccess::getPlayerActor() && msg >= ACTMES_PLAYER_PUNCH && msg < ACTMES_PLAYER_ATTACK_END;
    bool byStarBit = msg == ACTMES_STAR_PIECE_ATTACK;
    if (!byPlayer && !byStarBit && !port_actor_recently_held(sender->mHost)) {
        return;
    }
    int idx = sensorIndex(boss, receiver);
    Event e{};
    if (idx < 0 || !currentStage(&e.stage, &e.scenario)) {
        return;
    }
    auto it = w.idOf.find(boss);
    bool placed = it != w.idOf.end();
    e.kind = kWorldBossHit;
    e.id = placed ? it->second : ordinalOf(boss);
    e.nameHash = hashName(boss->mName);
    e.pos[0] = sender->mPosition.x - receiver->mPosition.x;
    e.pos[1] = sender->mPosition.y - receiver->mPosition.y;
    e.pos[2] = sender->mPosition.z - receiver->mPosition.z;
    e.a = (float)(msg | (uint32_t)idx << 16);
    e.b = (float)((sender->mType & 0xFFFF) | (placed ? 0x10000u : 0u));
    PortHostAllocScope host;
    send(e);
    if (logging()) port_log("world: hit %s (message %u on sensor %d), told the room", boss->mName, msg, idx);
}

extern "C" void port_world_update(void) {
    if (!enabled()) {
        return;
    }
    World& w = world();
    uint32_t stage;
    uint8_t scenario;
    if (!currentStage(&stage, &scenario)) {
        return;
    }
    w.live = true;
    w.frame++;
    PortHostAllocScope host;
    updatePendingGrab();
    port_actor_sync_update(stage, scenario);
    updateHits();

    // Arrivals: a player newly in this stage and scenario hears this visit's
    // defeats and pickups, so their world matches ours.
    for (int i = 0; i < kMaxRemotes; i++) {
        const RemoteView& v = netRemote(i);
        bool inStage = v.present && v.pose.stageHash == stage && (uint8_t)v.pose.scenario == scenario;
        bool known = w.here.count(v.id) != 0;
        if (inStage && !known) {
            w.here.insert(v.id);
            if (w.historyStage == stage && w.historyScenario == scenario && !w.history.empty()) {
                for (const Event& e : w.history) send(e);
                if (logging()) port_log("world: told player %u about %d events here", v.id, (int)w.history.size());
            }
        } else if (!inStage && known) {
            w.here.erase(v.id);
        }
    }

    if (w.inbox.empty()) {
        return;
    }
    std::vector< Event > events;
    events.swap(w.inbox);
    for (const Event& e : events) {
        if (e.stage != stage || e.scenario != scenario) {
            continue;  // another stage or scenario: not ours
        }
        if (e.kind == kWorldShootingStar) {
            auto it = w.actorOf.find(e.id);
            if (it != w.actorOf.end()) {
                w.shootSignals.insert(it->second);
            }
            continue;
        }
        if (e.kind == kWorldSwitch) {
            w.applyingSwitch = true;
            applySwitch(e);
            w.applyingSwitch = false;
            remember(e);
            continue;
        }
        if (e.kind == kWorldChipGot) {
            int type = (int)(e.id >> 24), index = (int)((e.id >> 16) & 0xFF), group = (int16_t)(e.id & 0xFFFF);
            if (port_chip_take_remote(type, group, index) && logging())
                port_log("world: %s chip %d of group %d taken by another player", type == 0 ? "blue" : "yellow", index, group);
            remember(e);
            continue;
        }
        if (e.kind == kWorldEventStar) {
            applyEventStar(e);
            remember(e);
            continue;
        }
        if (e.kind == kWorldBossHit) {
            uint32_t a = (uint32_t)e.a, b = (uint32_t)e.b;
            LiveActor* boss = nullptr;
            if (b & 0x10000u) {
                auto it = w.actorOf.find(e.id);
                boss = it != w.actorOf.end() ? static_cast< LiveActor* >(it->second) : nullptr;
            } else {
                boss = byOrdinal(e.nameHash, e.id);
            }
            if (boss == nullptr || MR::isDead(boss)) {
                if (logging()) port_log("world: a boss hit for an actor not here");
                continue;
            }
            World::PendingHit h{boss, (int)(a >> 16), a & 0xFFFFu, b & 0xFFFFu, {e.pos[0], e.pos[1], e.pos[2]}, w.frame + 180};
            if (logging()) port_log("world: another player hit %s (message %u)", boss->mName, h.msg);
            w.hits.push_back(h);
            continue;
        }
        if (e.kind == kWorldStarGot) {
            if (!w.grabbedHere && !w.hasPendingGrab) {
                if (logging()) port_log("world: another player grabbed power star %d: getting it here too", (int)e.id);
                w.hasPendingGrab = true;
                w.pendingGrab = (int)e.id;
                w.pendingGrabUntil = w.frame + 60 * 30;
            }
            continue;
        }
        if (e.kind == kWorldCoinsDropped || e.kind == kWorldStarBitsDropped) {
            if (logging()) port_log("world: another player dropped %u %s here", e.id, e.kind == kWorldCoinsDropped ? "coins" : "star bits");
            dropLocally(e);
            continue;
        }
        if (e.kind == kWorldCaught) {
            // Counted here even if it never showed here (dead or hidden).
            auto it = w.actorOf.find(e.id);
            if (it == w.actorOf.end() || w.catches.count(it->second) == 0) {
                continue;
            }
            if (port_runaway_rabbit_caught_remote(it->second)) {
                if (logging()) port_log("world: %s caught by another player: counted here", static_cast< LiveActor* >(it->second)->mName);
                remember(e);
            }
            continue;
        }
        LiveActor* actor = findLocal(e);
        if (actor == nullptr || MR::isDead(actor) || w.catches.count(actor) != 0) {
            continue;  // already gone here (or no match)
        }
        if (e.kind == kWorldEnemyDefeated) {
            if (logging()) port_log("world: %s defeated by another player", actor->mName);
            defeatLocally(actor);
        } else if (e.kind == kWorldItemCollected) {
            if (logging()) port_log("world: %s collected by another player%s", actor->mName, e.a != 0.0f ? " (purple: counted here too)" : "");
            collectLocally(actor);
            if (e.a != 0.0f) {
                port_coin_count_purple();
            }
        }
        remember(e);
    }
}

// Debug (F10): defeat the enemy nearest the local player, through its own
// kill() as a real defeat ends.  Returns false if none is in range.
extern "C" int port_world_debug_kill_nearest(float x, float y, float z) {
    World& w = world();
    LiveActor* best = nullptr;
    float bestD = 3000.0f * 3000.0f;
    int enemies = 0, spawned = 0;
    bool unplacedPass = false;
    auto consider = [&](void* p) {
        LiveActor* a = static_cast< LiveActor* >(p);
        if (MR::isDead(a) || w.items.count(p) != 0 || !hasEnemySensor(a)) {
            return;
        }
        enemies++;
        spawned += unplacedPass ? 1 : 0;
        float dx = a->mPosition.x - x, dy = a->mPosition.y - y, dz = a->mPosition.z - z;
        float d = dx * dx + dy * dy + dz * dz;
        if (d < bestD) {
            bestD = d;
            best = a;
        }
    };
    for (auto& kv : w.idOf) consider(kv.first);
    unplacedPass = true;
    for (void* p : w.unplaced) consider(p);
    port_log("world: debug: %d live enemies (%d spawned), nearest %s at %.0f (%.0f %.0f %.0f)", enemies, spawned, best ? best->mName : "-",
             best ? sqrtf(bestD) : -1.0f, best ? best->mPosition.x : 0.0f, best ? best->mPosition.y : 0.0f, best ? best->mPosition.z : 0.0f);
    if (best == nullptr) {
        return 0;
    }
    best->kill();
    return 1;
}

// Debug (F11): drop 3 coins and 5 star bits over the local player, as an
// enemy or a block would (testing the drop sync).
extern "C" void port_world_debug_drop(float x, float y, float z) {
    World& w = world();
    if (w.lootHost == nullptr) {
        port_log("world: debug: no loot host");
        return;
    }
    TVec3f pos(x, y + 200.0f, z);
    MR::appearCoinPop(static_cast< const NameObj* >(w.lootHost), pos, 3);
    MR::appearStarPiece(static_cast< const NameObj* >(w.lootHost), pos, 5, 10.0f, 40.0f, false);
}

// Debug (mode 103): the nearest declared event star host (a boss) is beaten: its
// star appears, as its defeat would request.
extern "C" void port_world_debug_event_star(float x, float y, float z) {
    World& w = world();
    LiveActor* best = nullptr;
    float bestD = 1e12f;
    for (auto& kv : w.idOf) {
        LiveActor* a = static_cast< LiveActor* >(kv.first);
        if (MR::isDead(a) || w.starNames.count(hashName(a->mName)) == 0) {
            continue;
        }
        float dx = a->mPosition.x - x, dy = a->mPosition.y - y, dz = a->mPosition.z - z;
        float d = dx * dx + dy * dy + dz * dz;
        if (d < bestD) {
            bestD = d;
            best = a;
        }
    }
    port_log("world: debug: %d event star names, nearest host %s", (int)w.starNames.size(), best ? best->mName : "-");
    if (best != nullptr) {
        best->kill();
        MR::requestAppearPowerStar(best, best->mPosition);
    }
}

// Debug: grab the nearest power star that waits to be grabbed.
extern "C" void port_world_debug_grab_star(void) {
    World& w = world();
    int pulled = 0;
    for (auto& s : w.powerStars) {
        if (port_power_star_pull_to_player(s.first) == 1) {
            port_log("world: debug: pulled power star %d to the player", s.second);
            pulled = 1;
            break;
        }
    }
    if (!pulled) port_log("world: debug: no power star waiting (%d in the scene)", (int)w.powerStars.size());
}

extern "C" const char* port_actor_nerve_name(void*, int*);

// Debug (mode 105): the local player hits the nearest boss (spin, stomp or
// ground pound, whichever its sensors take first), through the game's own
// message path, as a real hit would.
extern "C" void port_world_debug_hit_boss(float x, float y, float z) {
    AllLiveActorGroup* all = MR::getAllLiveActorGroup();
    LiveActor* mario = MarioAccess::getPlayerActor();
    HitSensor* body = mario != nullptr ? mario->getSensor("body") : nullptr;
    LiveActor* best = nullptr;
    float bestD = 1e12f;
    for (int i = 0; i < all->getObjNum(); i++) {
        LiveActor* a = all->getActor(i);
        if (a == nullptr || MR::isDead(a) || !port_actor_is_boss(a) || a->mSensorKeeper == nullptr) continue;
        float dx = a->mPosition.x - x, dy = a->mPosition.y - y, dz = a->mPosition.z - z;
        float d = dx * dx + dy * dy + dz * dz;
        if (d < bestD) {
            bestD = d;
            best = a;
        }
    }
    if (best == nullptr || body == nullptr) {
        port_log("world: debug: no boss near");
        static int once = 0;
        if (once++ == 0) {
            for (int i = 0; i < all->getObjNum(); i++) {
                LiveActor* a = all->getActor(i);
                int fam;
                const char* nn = a ? port_actor_nerve_name(a, &fam) : "";
                if (a && (fam == 4 || strstr(nn, "Koopa") || strstr(nn, "Dino") || strstr(nn, "unregistered")))
                    port_log("world: debug: actor %s nerve %s family %d dead %d", a->mName, nn, fam, MR::isDead(a) ? 1 : 0);
            }
        }
        return;
    }
    static const u32 kMsgs[] = {ACTMES_PLAYER_PUNCH, ACTMES_PLAYER_TRAMPLE, ACTMES_PLAYER_HIP_DROP};
    for (u32 m : kMsgs) {
        for (int i = 0; i < best->mSensorKeeper->mSensorCount; i++) {
            HitSensor* r = best->mSensorKeeper->getNthSensorInfo(i)->mSensor;
            if (r != nullptr && r->receiveMessage(m, body)) {
                port_log("world: debug: hit %s with message %u on sensor %d", best->mName, m, i);
                return;
            }
        }
    }
    port_log("world: debug: %s took no hit", best->mName);
}
