// World sync, first slice: defeated enemies.  See the header.
#include "world_sync.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <unordered_map>
#include <vector>

#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/HitSensorInfo.hpp"
#include "Game/LiveActor/HitSensorKeeper.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "net/capture/player_capture.h"
#include "net/client/net_client.h"
#include "port/heap_routing.h"
#include "port/port.h"

namespace net {

uint32_t worldPlacementId(const char* name, float x, float y, float z) {
    uint32_t h = 2166136261u;
    auto mix = [&](uint8_t b) {
        h ^= b;
        h *= 16777619u;
    };
    for (const char* p = name; p != nullptr && *p != 0; p++) mix((uint8_t)*p);
    const int32_t v[3] = {(int32_t)lroundf(x), (int32_t)lroundf(y), (int32_t)lroundf(z)};
    for (int32_t c : v) {
        for (int i = 0; i < 4; i++) mix((uint8_t)(c >> (8 * i)));
    }
    return h;
}

namespace {

struct World {
    std::unordered_map<void*, uint32_t> idOf;     // actor -> placement id
    std::unordered_map<uint32_t, void*> actorOf;  // placement id -> actor
    struct Event {
        uint8_t kind;
        uint32_t stage;
        uint8_t scenario;
        uint32_t id;
    };
    std::vector<Event> inbox;
    bool applying = false;  // killing for a remote event: do not echo it back
};

World& world() {
    static World* w = [] {
        PortHostAllocScope host;
        return new World();
    }();
    return *w;
}

bool hasEnemySensor(LiveActor* actor) {
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

bool enabled() {
    static const char* s = getenv("SMG_WORLD_SYNC");  // "0": off
    return net::netEnabled() && !(s != nullptr && s[0] == '0');
}

}  // namespace

void worldOnEvent(const uint8_t* data, unsigned len) {
    if (len != kWorldEventSize || data[0] != kMsgWorldEvent) {
        return;
    }
    PortHostAllocScope host;
    world().inbox.push_back({data[1], get32(data + 2), data[6], get32(data + 7)});
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
    for (uint32_t salt = 1; w.actorOf.count(id) != 0 && salt < 64; salt++) {
        id = worldPlacementId(name, x, y, z) + salt * 0x9E3779B9u;
    }
    w.idOf[actor] = id;
    w.actorOf[id] = actor;
}

extern "C" void port_world_scene_reset(void) {
    PortHostAllocScope host;
    World& w = world();
    w.idOf.clear();
    w.actorOf.clear();
    w.inbox.clear();
}

extern "C" void port_world_actor_dead(void* actorPtr) {
    LiveActor* actor = static_cast< LiveActor* >(actorPtr);
    if (!enabled() || world().applying || MR::isDead(actor)) {
        return;  // already dead (initial makeActorDead, repeats)
    }
    auto it = world().idOf.find(actorPtr);
    if (it == world().idOf.end() || !hasEnemySensor(actor)) {
        return;
    }
    NetPlayerCapture now;
    if (!net_capture_latest(&now)) {
        return;  // not in gameplay yet: scene set-up hides actors this way too
    }
    uint8_t m[kWorldEventSize];
    m[0] = kMsgWorldEvent;
    m[1] = kWorldEnemyDefeated;
    put32(m + 2, now.stageHash);
    m[6] = (uint8_t)now.scenario;
    put32(m + 7, it->second);
    netSendRaw(m, sizeof(m));
    static const bool sLog = getenv("PETARI_WORLDLOG") != nullptr;
    if (sLog) {
        port_log("world: defeated %s (id %08x), told the room", actor->mName, it->second);
    }
}

extern "C" void port_world_update(void) {
    World& w = world();
    if (w.inbox.empty()) {
        return;
    }
    NetPlayerCapture now;
    bool here = net_capture_latest(&now) != 0;
    PortHostAllocScope host;
    std::vector< World::Event > events;
    events.swap(w.inbox);
    for (const World::Event& e : events) {
        if (!here || e.stage != now.stageHash || e.scenario != (uint8_t)now.scenario || e.kind != kWorldEnemyDefeated) {
            continue;  // another stage or scenario: not ours
        }
        auto it = w.actorOf.find(e.id);
        if (it == w.actorOf.end()) {
            port_log("world: unknown enemy id %08x", e.id);
            continue;
        }
        LiveActor* actor = static_cast< LiveActor* >(it->second);
        if (MR::isDead(actor)) {
            continue;
        }
        port_log("world: %s defeated by another player", actor->mName);
        w.applying = true;
        actor->kill();
        w.applying = false;
    }
}

// Debug (F10): defeat the registered enemy nearest the local player, through
// its own kill() as a real defeat ends.  Returns false if none is in range.
extern "C" int port_world_debug_kill_nearest(float x, float y, float z) {
    World& w = world();
    LiveActor* best = nullptr;
    float bestD = 3000.0f * 3000.0f;
    int enemies = 0;
    for (auto& kv : w.idOf) {
        LiveActor* a = static_cast< LiveActor* >(kv.first);
        if (MR::isDead(a) || !hasEnemySensor(a)) {
            continue;
        }
        enemies++;
        float dx = a->mPosition.x - x, dy = a->mPosition.y - y, dz = a->mPosition.z - z;
        float d = dx * dx + dy * dy + dz * dz;
        if (d < bestD) {
            bestD = d;
            best = a;
        }
    }
    port_log("world: debug: %d live enemies registered, nearest %s at %.0f (%.0f %.0f %.0f)", enemies, best ? best->mName : "-",
             best ? sqrtf(bestD) : -1.0f, best ? best->mPosition.x : 0.0f, best ? best->mPosition.y : 0.0f, best ? best->mPosition.z : 0.0f);
    if (best == nullptr) {
        return 0;
    }
    best->kill();
    return 1;
}
