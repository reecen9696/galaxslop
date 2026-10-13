// World sync, first slice: defeated enemies.  See the header.
#include "world_sync.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/HitSensorInfo.hpp"
#include "Game/LiveActor/HitSensorKeeper.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
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

// A defeat as sent and received.  `id` is the placement id, 0 for an enemy
// that was not placed from the stage data (spawned), which receivers match
// by name and position instead.
struct Defeat {
    uint32_t id;
    uint32_t nameHash;
    float pos[3];
};

struct World {
    std::unordered_map<void*, uint32_t> idOf;     // placed actor -> placement id
    std::unordered_map<uint32_t, void*> actorOf;  // placement id -> placed actor
    std::unordered_set<void*> unplaced;           // actors that appeared without a placement id
    struct Event {
        uint32_t stage;
        uint8_t scenario;
        Defeat d;
    };
    std::vector<Event> inbox;
    // Defeats in this visit (local or received), told to players who arrive
    // in the stage later.
    std::vector<Defeat> defeated;
    uint32_t defeatedStage = 0;
    uint8_t defeatedScenario = 0xFF;
    // Remote players already in this stage (by id), to spot arrivals.
    std::unordered_set<uint8_t> here;
    bool applying = false;  // killing for a remote event: no echo, no loot
    // Gameplay of this scene has begun (first port_world_update).  Scene
    // set-up kills and hides actors too (boss parts, pooled objects); those
    // are not defeats.
    bool live = false;
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
    // star chips, "...チップ") are per player, not defeats.
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

void send(const Defeat& d, uint32_t stage, uint8_t scenario) {
    uint8_t m[kWorldEventSize];
    m[0] = kMsgWorldEvent;
    m[1] = kWorldEnemyDefeated;
    put32(m + 2, stage);
    m[6] = scenario;
    put32(m + 7, d.id);
    put32(m + 11, d.nameHash);
    for (int i = 0; i < 3; i++) putF(m + 15 + 4 * i, d.pos[i]);
    netSendRaw(m, sizeof(m));
}

// The visit's defeat list belongs to one stage and scenario.
void rememberDefeat(const Defeat& d, uint32_t stage, uint8_t scenario) {
    World& w = world();
    if (stage != w.defeatedStage || scenario != w.defeatedScenario) {
        w.defeated.clear();
        w.defeatedStage = stage;
        w.defeatedScenario = scenario;
    }
    w.defeated.push_back(d);
}

// The local copy of a defeated enemy: by placement id, else (spawned
// enemies) the nearest live one of the same name within 400 units.
LiveActor* findLocal(const Defeat& d) {
    World& w = world();
    if (d.id != 0) {
        auto it = w.actorOf.find(d.id);
        return it != w.actorOf.end() ? static_cast< LiveActor* >(it->second) : nullptr;
    }
    LiveActor* best = nullptr;
    float bestD = 400.0f * 400.0f;
    for (void* p : w.unplaced) {
        LiveActor* a = static_cast< LiveActor* >(p);
        if (MR::isDead(a) || hashName(a->mName) != d.nameHash) {
            continue;
        }
        float dx = a->mPosition.x - d.pos[0], dy = a->mPosition.y - d.pos[1], dz = a->mPosition.z - d.pos[2];
        float dist = dx * dx + dy * dy + dz * dz;
        if (dist < bestD) {
            bestD = dist;
            best = a;
        }
    }
    return best;
}

// The defeat as the other player saw it: the enemy's own death effect and
// the standard defeat sound where it stands, then its own kill() (cleanup,
// parts), with the loot its kill() may drop suppressed (the defeating
// player got that).
void defeatLocally(LiveActor* actor) {
    World& w = world();
    if (MR::isRegisteredEffect(actor, "Death")) {
        MR::emitEffect(actor, "Death");
    }
    MR::startSound(actor, "SE_EM_EXPLODE_S", -1, -1);
    w.applying = true;
    actor->kill();
    w.applying = false;
}

}  // namespace

void worldOnEvent(const uint8_t* data, unsigned len) {
    if (len != kWorldEventSize || data[0] != kMsgWorldEvent || data[1] != kWorldEnemyDefeated) {
        return;
    }
    PortHostAllocScope host;
    World::Event e;
    e.stage = get32(data + 2);
    e.scenario = data[6];
    e.d.id = get32(data + 7);
    e.d.nameHash = get32(data + 11);
    for (int i = 0; i < 3; i++) e.d.pos[i] = getF(data + 15 + 4 * i);
    world().inbox.push_back(e);
}

}  // namespace net

using namespace net;

extern "C" void port_world_register(void* actor, const char* name, float x, float y, float z) {
    if (!enabled()) {
        return;
    }
    PortHostAllocScope host;
    World& w = world();
    uint32_t id = worldPlacementId(name, x, y, z);
    // Same name at the same spot: the next ids by registration order, which
    // follows the stage data and so matches on every client.
    for (uint32_t salt = 1; (id == 0 || w.actorOf.count(id) != 0) && salt < 64; salt++) {
        id = worldPlacementId(name, x, y, z) + salt * 0x9E3779B9u;
    }
    w.idOf[actor] = id;
    w.actorOf[id] = actor;
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

extern "C" void port_world_scene_reset(void) {
    PortHostAllocScope host;
    World& w = world();
    w.idOf.clear();
    w.actorOf.clear();
    w.unplaced.clear();
    w.inbox.clear();
    w.defeated.clear();
    w.defeatedScenario = 0xFF;
    w.here.clear();
    w.live = false;
}

extern "C" int port_world_suppress_loot(void) {
    if (!world().applying) {
        return 0;
    }
    if (logging()) port_log("world: loot from a remote defeat suppressed");
    return 1;
}

extern "C" void port_world_actor_dead(void* actorPtr) {
    LiveActor* actor = static_cast< LiveActor* >(actorPtr);
    World& w = world();
    if (!enabled() || !w.live || w.applying || MR::isDead(actor)) {
        return;  // scene set-up, a remote defeat, or already dead
    }
    auto it = w.idOf.find(actorPtr);
    bool placed = it != w.idOf.end();
    if ((!placed && w.unplaced.count(actorPtr) == 0) || !hasEnemySensor(actor)) {
        return;
    }
    NetPlayerCapture now;
    if (!net_capture_latest(&now)) {
        return;  // not in gameplay yet: scene set-up hides actors this way too
    }
    Defeat d;
    d.id = placed ? it->second : 0;
    d.nameHash = hashName(actor->mName);
    d.pos[0] = actor->mPosition.x;
    d.pos[1] = actor->mPosition.y;
    d.pos[2] = actor->mPosition.z;
    PortHostAllocScope host;
    send(d, now.stageHash, (uint8_t)now.scenario);
    rememberDefeat(d, now.stageHash, (uint8_t)now.scenario);
    if (logging()) {
        port_log("world: defeated %s (%s %08x), told the room", actor->mName, placed ? "id" : "spawned", d.id);
    }
}

extern "C" void port_world_update(void) {
    if (!enabled()) {
        return;
    }
    World& w = world();
    NetPlayerCapture now;
    if (!net_capture_latest(&now)) {
        return;
    }
    w.live = true;
    PortHostAllocScope host;
    uint8_t scenario = (uint8_t)now.scenario;

    // Arrivals: a player newly in this stage and scenario hears this visit's
    // defeats, so their enemies match ours.
    for (int i = 0; i < kMaxRemotes; i++) {
        const RemoteView& v = netRemote(i);
        bool inStage = v.present && v.pose.stageHash == now.stageHash && v.pose.scenario == now.scenario;
        bool known = w.here.count(v.id) != 0;
        if (inStage && !known) {
            w.here.insert(v.id);
            if (w.defeatedStage == now.stageHash && w.defeatedScenario == scenario && !w.defeated.empty()) {
                for (const Defeat& d : w.defeated) send(d, now.stageHash, scenario);
                if (logging()) port_log("world: told player %u about %d defeats here", v.id, (int)w.defeated.size());
            }
        } else if (!inStage && known) {
            w.here.erase(v.id);
        }
    }

    if (w.inbox.empty()) {
        return;
    }
    std::vector< World::Event > events;
    events.swap(w.inbox);
    for (const World::Event& e : events) {
        if (e.stage != now.stageHash || e.scenario != scenario) {
            continue;  // another stage or scenario: not ours
        }
        LiveActor* actor = findLocal(e.d);
        if (actor == nullptr || MR::isDead(actor)) {
            continue;  // already gone here (or no match)
        }
        if (logging()) port_log("world: %s defeated by another player", actor->mName);
        defeatLocally(actor);
        rememberDefeat(e.d, e.stage, e.scenario);
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
        if (MR::isDead(a) || !hasEnemySensor(a)) {
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
    port_log("world: debug: %d live enemies (%d spawned), nearest %s at %.0f (%.0f %.0f %.0f)", enemies, spawned, best ? best->mName : "-", best ? sqrtf(bestD) : -1.0f,
             best ? best->mPosition.x : 0.0f, best ? best->mPosition.y : 0.0f, best ? best->mPosition.z : 0.0f);
    if (best == nullptr) {
        return 0;
    }
    best->kill();
    return 1;
}
