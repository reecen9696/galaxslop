// Actor state sync (P4-03).  See the header.
#include "actor_sync.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/RailRider.hpp"
#include "Game/LiveActor/Spine.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapPartsRailMover.hpp"
#include "Game/MapObj/MapPartsRailRotator.hpp"
#include "Game/MapObj/MapPartsRotator.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "net/client/net_client.h"
#include "net/world/world_sync.h"
#include "port/heap_routing.h"
#include "port/port.h"

// ---- Nerve registry ----
// Filled by static initialisers (NEW_NERVE), before any heap exists: fixed
// tables in zeroed static storage, no allocation.

namespace {

enum Family : uint8_t {
    kFamOther = 0,  // not known (no nerve yet, or outside the folders below)
    kFamMapObj,     // Game/MapObj, Game/Map
    kFamRide,       // Game/Ride
    kFamEnemy,      // Game/Enemy
    kFamBoss,       // Game/Boss: hits are replayed instead (not here)
    kFamNpc,        // Game/NPC
};

struct NerveInfo {
    const void* nerve;
    const char* name;  // "Class::Nerve" (a string literal)
    uint32_t hash;
    uint8_t family;
    bool deny;  // never forced on a copy (talks, demos, binding the player...)
};

constexpr int kNerveSlots = 16384;  // power of two, over 4x the nerves
NerveInfo gNerves[kNerveSlots];     // open addressing by pointer
int32_t gByHash[kNerveSlots];       // slot + 1 by name hash; -1 two names share it

uint32_t fnvStr(const char* s) {
    uint32_t h = 2166136261u;
    for (; s != nullptr && *s != 0; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

uint32_t ptrSlot(const void* p) { return (uint32_t)(((uintptr_t)p >> 3) * 2654435761u) & (kNerveSlots - 1); }

const NerveInfo* nerveInfo(const void* nerve) {
    if (nerve == nullptr) return nullptr;
    for (uint32_t i = ptrSlot(nerve), n = 0; n < kNerveSlots; i = (i + 1) & (kNerveSlots - 1), n++) {
        if (gNerves[i].nerve == nerve) return &gNerves[i];
        if (gNerves[i].nerve == nullptr) return nullptr;
    }
    return nullptr;
}

const NerveInfo* nerveByHash(uint32_t hash) {
    if (hash == 0) return nullptr;
    for (uint32_t i = hash & (kNerveSlots - 1), n = 0; n < kNerveSlots; i = (i + 1) & (kNerveSlots - 1), n++) {
        int32_t v = gByHash[i];
        if (v == 0) return nullptr;
        if (v > 0 && gNerves[v - 1].hash == hash) return &gNerves[v - 1];
    }
    return nullptr;
}

uint8_t familyOfFile(const char* file) {
    if (file == nullptr) return kFamOther;
    if (strstr(file, "/Game/MapObj/") || strstr(file, "/Game/Map/")) return kFamMapObj;
    if (strstr(file, "/Game/Ride/")) return kFamRide;
    if (strstr(file, "/Game/Enemy/")) return kFamEnemy;
    if (strstr(file, "/Game/Boss/")) return kFamBoss;
    if (strstr(file, "/Game/NPC/")) return kFamNpc;
    return kFamOther;
}

bool deniedName(const char* name) {
    static const char* const kDeny[] = {"Talk", "Demo", "Bind", "Rush", "Take", "Hold", "Carry", "Catch", "Throw", "Event", "Ride", "Capture", "Swallow"};
    const char* n = strstr(name, "::");
    n = n ? n + 2 : name;
    for (const char* d : kDeny) {
        if (strstr(n, d) != nullptr) return true;
    }
    return false;
}

// An NPC in one of these states is busy with the local player: talking,
// racing him, running from him...
bool busyNpcNerve(const char* name) {
    static const char* const kBusy[] = {"Talk", "Race", "Ready", "Pre", "Goal", "Post", "TakeOutStar", "Demo", "Event", "Caught", "Run", "Escape", "Start"};
    const char* n = strstr(name, "Nrv");
    n = n ? n + 3 : (strstr(name, "::") ? strstr(name, "::") + 2 : name);
    for (const char* b : kBusy) {
        if (strncmp(n, b, strlen(b)) == 0) return true;
    }
    return false;
}

}  // namespace

extern "C" int port_nerve_register(const void* nerve, const char* name, const char* file) {
    if (nerve == nullptr || nerveInfo(nerve) != nullptr) return 0;
    uint32_t i = ptrSlot(nerve);
    for (int n = 0; n < kNerveSlots && gNerves[i].nerve != nullptr; n++) i = (i + 1) & (kNerveSlots - 1);
    if (gNerves[i].nerve != nullptr) return 0;  // full
    NerveInfo& e = gNerves[i];
    e.nerve = nerve;
    e.name = name;
    e.hash = fnvStr(name);
    if (e.hash == 0) e.hash = 1;
    e.family = familyOfFile(file);
    e.deny = deniedName(name);
    // By hash: a second nerve of the same name makes the name unusable.
    for (uint32_t j = e.hash & (kNerveSlots - 1), n = 0; n < kNerveSlots; j = (j + 1) & (kNerveSlots - 1), n++) {
        int32_t v = gByHash[j];
        if (v == 0) {
            gByHash[j] = (int32_t)i + 1;
            break;
        }
        if (v > 0 && gNerves[v - 1].hash == e.hash) {
            gByHash[j] = -1;
            break;
        }
    }
    return 1;
}

// ---- Tracked actors ----

namespace net {

void actorStateOnMessage(const uint8_t* data, unsigned len);

namespace {

constexpr int kSendEvery = 3;          // frames between ActorState messages
constexpr unsigned kMaxMessage = 1400; // bytes per message
constexpr uint32_t kClaimFrames = 90;  // a touch keeps the actor 1.5 s
constexpr float kHandoverMargin = 300.0f;
constexpr float kNear = 4000.0f;       // near the local player: full rate

enum Flags : uint8_t {
    kFlagDead = 1,
    kFlagHidden = 2,
    kFlagCollision = 4,   // collision valid
    kFlagClaim = 8,       // the sender touches it (stands on, holds, hits)
};
enum Extras : uint8_t {
    kExRail = 1,        // RailRider: coord, speed, direction
    kExRailMover = 2,   // MapPartsRailMover
    kExRotator = 4,     // MapPartsRotator
    kExRailRotator = 8, // MapPartsRailRotator
    kExAnim = 16,       // the model's animation (BCK): name, frame, rate
};

struct Pose {
    float pos[3];
    float q[4];  // x y z w
};

struct Snap {
    Pose pose;
    uint32_t senderFrame = 0;
    uint32_t recvFrame = 0;
};

struct Tracked {
    LiveActor* actor = nullptr;
    uint32_t id = 0;
    uint8_t family = kFamOther;
    bool familyKnown = false;
    bool enemySensor = false;
    bool excluded = false;  // items, the sky...: the same everywhere or synced elsewhere
    MapPartsRailMover* railMover = nullptr;
    MapPartsRotator* rotator = nullptr;
    MapPartsRailRotator* railRotator = nullptr;
    // Ownership.
    uint8_t owner = 0;           // player id (0: nobody yet)
    uint32_t claimUntil = 0;     // the local player touched it: ours until
    bool busy = false;           // an NPC busy with the local player (talk, race): never driven
    bool killedByOwner = false;  // killed here because it was gone for its owner
    uint32_t remoteClaimUntil = 0;
    // Owner side.
    bool haveBase = false;
    Pose base;
    bool haveBaseState = false;
    uint32_t baseNerve = 0;
    uint8_t baseFlags = 0;
    bool dynamic = false;  // moved or changed state since the scene began
    bool everSent = false;
    Pose sent;
    uint32_t sentNerve = 0;
    uint8_t sentFlags = 0;
    uint32_t sentFrame = 0;
    uint32_t sentAnim = 0;  // hash of the BCK name last sent
    // Copy side.
    int snaps = 0;
    Snap last, prev;
    uint32_t ownerNerve = 0;
    uint16_t ownerStep = 0;
    uint8_t ownerFlags = 0;
    bool nerveDirty = false;      // owner's nerve changed: follow it
    uint32_t mismatchSince = 0;
    bool shown = false;
    Pose shownPose;
    char ownerAnim[48] = {};
    float ownerAnimFrame = 0.0f;
    float ownerAnimRate = 0.0f;
    uint32_t ownerAnimFrameAt = 0;  // local frame it was received
};

struct State {
    std::vector<Tracked> list;
    std::unordered_map<void*, int> index;
    std::unordered_map<uint32_t, int> byId;
    // MapParts made before their host registered: host -> parts.
    std::vector<std::pair<void*, std::pair<int, void*>>> parts;
    uint32_t frame = 0;
    uint32_t stage = 0;
    uint8_t scenario = 0xFF;
    bool live = false;
    LiveActor* standing = nullptr;  // what the local player stands on
    LiveActor* holding = nullptr;   // what the local player holds
    LiveActor* lastHeld = nullptr;  // ... or held last, and until when it counts
    uint32_t lastHeldUntil = 0;
    size_t cursor = 0;
    // Received, applied on the next update.
    std::vector<std::vector<uint8_t>> inbox;
    std::unordered_map<void*, bool> bosses;  // placed bosses (by object name)
    uint32_t sentMsgs = 0, sentEntries = 0, recvEntries = 0;
};

State& st() {
    static State* s = [] {
        PortHostAllocScope host;
        return new State();
    }();
    return *s;
}

bool logging() {
    static const bool s = getenv("PETARI_ACTORLOG") != nullptr;
    return s;
}

bool enabled() {
    static const char* s = getenv("SMG_ACTOR_SYNC");  // "0": off
    return net::netEnabled() && !(s != nullptr && s[0] == '0');
}

Tracked* find(const void* actor) {
    State& s = st();
    auto it = s.index.find(const_cast<void*>(actor));
    return it == s.index.end() ? nullptr : &s.list[(size_t)it->second];
}

// ---- Math ----

void mtxToPose(MtxPtr m, Pose* p) {
    p->pos[0] = m[0][3];
    p->pos[1] = m[1][3];
    p->pos[2] = m[2][3];
    float m00 = m[0][0], m11 = m[1][1], m22 = m[2][2];
    float tr = m00 + m11 + m22;
    float x, y, z, w;
    if (tr > 0.0f) {
        float s = sqrtf(tr + 1.0f) * 2.0f;
        w = 0.25f * s;
        x = (m[2][1] - m[1][2]) / s;
        y = (m[0][2] - m[2][0]) / s;
        z = (m[1][0] - m[0][1]) / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
        w = (m[2][1] - m[1][2]) / s;
        x = 0.25f * s;
        y = (m[0][1] + m[1][0]) / s;
        z = (m[0][2] + m[2][0]) / s;
    } else if (m11 > m22) {
        float s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
        w = (m[0][2] - m[2][0]) / s;
        x = (m[0][1] + m[1][0]) / s;
        y = 0.25f * s;
        z = (m[1][2] + m[2][1]) / s;
    } else {
        float s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
        w = (m[1][0] - m[0][1]) / s;
        x = (m[0][2] + m[2][0]) / s;
        y = (m[1][2] + m[2][1]) / s;
        z = 0.25f * s;
    }
    float n = sqrtf(x * x + y * y + z * z + w * w);
    if (n < 1e-6f) {
        x = y = z = 0.0f;
        w = n = 1.0f;
    }
    p->q[0] = x / n;
    p->q[1] = y / n;
    p->q[2] = z / n;
    p->q[3] = w / n;
}

void poseToMtx(const Pose& p, MtxPtr m) {
    float x = p.q[0], y = p.q[1], z = p.q[2], w = p.q[3];
    m[0][0] = 1 - 2 * (y * y + z * z);
    m[0][1] = 2 * (x * y - z * w);
    m[0][2] = 2 * (x * z + y * w);
    m[1][0] = 2 * (x * y + z * w);
    m[1][1] = 1 - 2 * (x * x + z * z);
    m[1][2] = 2 * (y * z - x * w);
    m[2][0] = 2 * (x * z - y * w);
    m[2][1] = 2 * (y * z + x * w);
    m[2][2] = 1 - 2 * (x * x + y * y);
    m[0][3] = p.pos[0];
    m[1][3] = p.pos[1];
    m[2][3] = p.pos[2];
}

float qdot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]; }

void qnorm(float* q) {
    float n = sqrtf(qdot(q, q));
    if (n < 1e-6f) {
        q[0] = q[1] = q[2] = 0.0f;
        q[3] = 1.0f;
        return;
    }
    for (int i = 0; i < 4; i++) q[i] /= n;
}

void qmul(const float* a, const float* b, float* o) {
    float r[4] = {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                  a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
    memcpy(o, r, sizeof(r));
}

// Normalised lerp from a towards b by t (shortest way).
void qblend(const float* a, const float* b, float t, float* o) {
    float s = qdot(a, b) < 0.0f ? -1.0f : 1.0f;
    for (int i = 0; i < 4; i++) o[i] = a[i] + (s * b[i] - a[i]) * t;
    qnorm(o);
}

// q raised to the power t (rotation scaled), for extrapolating a spin.
void qpow(const float* q, float t, float* o) {
    float w = q[3] < -1.0f ? -1.0f : (q[3] > 1.0f ? 1.0f : q[3]);
    float sgn = w < 0.0f ? -1.0f : 1.0f;  // shortest arc
    float ang = acosf(w * sgn);
    float s = sinf(ang);
    if (s < 1e-5f) {
        o[0] = o[1] = o[2] = 0.0f;
        o[3] = 1.0f;
        return;
    }
    float na = ang * t;
    float k = sinf(na) / s * sgn;
    o[0] = q[0] * k;
    o[1] = q[1] * k;
    o[2] = q[2] * k;
    o[3] = cosf(na);
}

float dist2(const float* a, const float* b) {
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

// ---- Wire ----

void put16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back((uint8_t)v);
    b.push_back((uint8_t)(v >> 8));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i)));
}
void putF(std::vector<uint8_t>& b, float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    put32(b, u);
}

struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;
    bool need(unsigned n) {
        if (!ok || (unsigned)(end - p) < n) ok = false;
        return ok;
    }
    uint8_t u8() { return need(1) ? *p++ : 0; }
    uint16_t u16() {
        if (!need(2)) return 0;
        uint16_t v = (uint16_t)(p[0] | p[1] << 8);
        p += 2;
        return v;
    }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        p += 4;
        return v;
    }
    float f() {
        uint32_t u = u32();
        float v;
        memcpy(&v, &u, 4);
        return v;
    }
};

uint32_t nerveHashOf(const Spine* spine) {
    if (spine == nullptr) return 0;
    const NerveInfo* n = nerveInfo(spine->getCurrentNerve());
    return n ? n->hash : 0;
}

uint16_t stepOf(const Spine* spine) {
    if (spine == nullptr || spine->mStep < 0) return 0;
    return spine->mStep > 0xFFFF ? 0xFFFF : (uint16_t)spine->mStep;
}

// A MapParts spine set to the owner's state exactly (no entry code: the
// part's fields come with it).
void setPartSpine(Spine* spine, uint32_t hash, uint16_t step) {
    const NerveInfo* n = nerveByHash(hash);
    if (spine == nullptr || n == nullptr) return;
    spine->mCurrNerve = static_cast<const Nerve*>(n->nerve);
    spine->mNextNerve = nullptr;
    spine->mStep = step;
}

// What the families sync.
bool syncsTransform(const Tracked& t) { return t.family != kFamBoss && !t.excluded; }
bool syncsNerve(const Tracked& t) {
    return t.family == kFamMapObj || t.family == kFamRide || t.family == kFamEnemy || t.family == kFamOther;
}
bool syncsFlags(const Tracked& t) { return t.family == kFamMapObj || t.family == kFamRide || t.family == kFamOther; }

// Actors left alone, by the class of their nerves: collectibles (synced as
// pickups), the sky and the like (drawn the same everywhere).
bool excludedClass(const char* nerveName) {
    static const char* const kSkip[] = {"Sky::", "StarPiece::", "Coin::", "PurpleCoin", "PowerStar::", "GrandStar::", "ChipBase::",
                                        "ScenarioStarter", "MarioActor::", "ShootingStar::"};
    for (const char* k : kSkip) {
        if (strstr(nerveName, k) == nerveName) return true;
    }
    return false;
}

void learnFamily(Tracked& t) {
    if (t.familyKnown) return;
    if (t.actor->mSpine != nullptr) {
        if (const NerveInfo* n = nerveInfo(t.actor->mSpine->getCurrentNerve())) {
            t.family = n->family;
            t.familyKnown = true;
            if (excludedClass(n->name)) t.excluded = true;
            if (t.enemySensor && t.family == kFamOther) t.family = kFamEnemy;
            return;
        }
    }
    t.family = t.enemySensor ? kFamEnemy : kFamOther;
}

// This client runs the actor: nobody else does, or we do.
bool ownedHere(const Tracked& t) { return t.owner == 0 || t.owner == netLocalId(); }

// Another player runs it and has told us recently.
bool driven(const Tracked& t) { return !ownedHere(t) && t.snaps > 0 && syncsTransform(t); }

void writeEntry(std::vector<uint8_t>& b, Tracked& t, const Pose& p, uint8_t flags) {
    LiveActor* a = t.actor;
    put32(b, t.id);
    put32(b, nerveHashOf(a->mSpine));
    put16(b, stepOf(a->mSpine));
    b.push_back(flags);
    for (int i = 0; i < 3; i++) putF(b, p.pos[i]);
    for (int i = 0; i < 4; i++) {
        float c = p.q[i] < -1.0f ? -1.0f : (p.q[i] > 1.0f ? 1.0f : p.q[i]);
        put16(b, (uint16_t)(int16_t)lroundf(c * 32767.0f));
    }
    uint8_t ex = 0;
    if (a->mRailRider != nullptr) ex |= kExRail;
    if (t.railMover != nullptr) ex |= kExRailMover;
    if (t.rotator != nullptr) ex |= kExRotator;
    if (t.railRotator != nullptr) ex |= kExRailRotator;
    const char* anim = a->mModelManager != nullptr ? MR::getPlayingBckName(a) : nullptr;
    if (anim != nullptr && *anim != 0 && strlen(anim) < sizeof(Tracked::ownerAnim)) ex |= kExAnim;
    b.push_back(ex);
    if (ex & kExRail) {
        RailRider* r = a->mRailRider;
        putF(b, r->mCoord);
        putF(b, r->mSpeed);
        b.push_back(r->mIsNotReverse ? 1 : 0);
    }
    if (ex & kExRailMover) {
        MapPartsRailMover* m = t.railMover;
        put32(b, nerveHashOf(m->mSpine));
        put16(b, stepOf(m->mSpine));
        putF(b, m->mSpeed);
        putF(b, m->mAcceleration);
        putF(b, m->_34);
        putF(b, m->_48);
    }
    if (ex & kExRotator) {
        MapPartsRotator* r = t.rotator;
        put32(b, nerveHashOf(r->mSpine));
        put16(b, stepOf(r->mSpine));
        putF(b, r->_18);
        putF(b, r->mTargetAngle);
        putF(b, r->mRotateSpeed);
        putF(b, r->mAngle);
        putF(b, r->_A0);
    }
    if (ex & kExRailRotator) {
        MapPartsRailRotator* r = t.railRotator;
        put32(b, nerveHashOf(r->mSpine));
        put16(b, stepOf(r->mSpine));
        putF(b, r->mAngle);
        putF(b, r->mTargetAngle);
        putF(b, r->mRotateSpeed);
    }
    if (ex & kExAnim) {
        size_t n = strlen(anim);
        b.push_back((uint8_t)n);
        b.insert(b.end(), anim, anim + n);
        putF(b, MR::getBckFrame(a));
        putF(b, MR::getBckRate(a));
    }
}

bool currentPose(const Tracked& t, Pose* p) {
    MtxPtr m = t.actor->getBaseMtx();
    if (m == nullptr) return false;
    mtxToPose(m, p);
    return true;
}

uint8_t flagsOf(const Tracked& t) {
    LiveActor* a = t.actor;
    uint8_t f = 0;
    if (MR::isDead(a)) f |= kFlagDead;
    if (MR::isHiddenModel(a)) f |= kFlagHidden;
    if (a->mCollisionParts != nullptr && MR::isValidCollisionParts(a)) f |= kFlagCollision;
    if (t.claimUntil > st().frame) f |= kFlagClaim;
    return f;
}

// Players in this stage: id and position (the local one first).
struct Player {
    uint8_t id;
    float pos[3];
};

int playersHere(Player* out, int max) {
    State& s = st();
    int n = 0;
    const TVec3f* me = MR::getPlayerPos();
    out[n++] = {netLocalId(), {me->x, me->y, me->z}};
    for (int i = 0; i < kMaxRemotes && n < max; i++) {
        const RemoteView& v = netRemote(i);
        if (v.present && v.visible && v.pose.stageHash == s.stage && (uint8_t)v.pose.scenario == s.scenario) {
            out[n++] = {v.id, {v.pose.pos[0], v.pose.pos[1], v.pose.pos[2]}};
        }
    }
    return n;
}

void claim(Tracked& t) {
    State& s = st();
    if (!t.busy && t.remoteClaimUntil > s.frame && t.owner != 0 && t.owner < netLocalId()) {
        return;  // a player with a lower id touches it too and runs it: ride along
    }
    bool was = ownedHere(t) && t.owner != 0;
    t.owner = netLocalId();
    t.claimUntil = s.frame + kClaimFrames;
    t.remoteClaimUntil = 0;
    t.nerveDirty = false;
    if (!was && logging()) port_log("actors: took %s (%08x): the local player touched it", t.actor->mName, t.id);
}

// ---- Applying the owner's state to the local copy ----

void applyExtras(Tracked& t, Reader& r, uint8_t ex) {
    LiveActor* a = t.actor;
    if (ex & kExRail) {
        float coord = r.f(), speed = r.f();
        bool fwd = r.u8() != 0;
        if (r.ok && a->mRailRider != nullptr) {
            a->mRailRider->mIsNotReverse = fwd;
            a->mRailRider->setCoord(coord);
            a->mRailRider->mSpeed = speed;
        }
    }
    if (ex & kExRailMover) {
        uint32_t h = r.u32();
        uint16_t step = r.u16();
        float speed = r.f(), accel = r.f(), v34 = r.f(), v48 = r.f();
        if (r.ok && t.railMover != nullptr) {
            setPartSpine(t.railMover->mSpine, h, step);
            t.railMover->mSpeed = speed;
            t.railMover->mAcceleration = accel;
            t.railMover->_34 = v34;
            t.railMover->_48 = v48;
        }
    }
    if (ex & kExRotator) {
        uint32_t h = r.u32();
        uint16_t step = r.u16();
        float v18 = r.f(), target = r.f(), speed = r.f(), angle = r.f(), vA0 = r.f();
        if (r.ok && t.rotator != nullptr) {
            setPartSpine(t.rotator->mSpine, h, step);
            t.rotator->_18 = v18;
            t.rotator->mTargetAngle = target;
            t.rotator->mRotateSpeed = speed;
            t.rotator->mAngle = angle;
            t.rotator->_A0 = vA0;
        }
    }
    if (ex & kExRailRotator) {
        uint32_t h = r.u32();
        uint16_t step = r.u16();
        float angle = r.f(), target = r.f(), speed = r.f();
        if (r.ok && t.railRotator != nullptr) {
            setPartSpine(t.railRotator->mSpine, h, step);
            t.railRotator->mAngle = angle;
            t.railRotator->mTargetAngle = target;
            t.railRotator->mRotateSpeed = speed;
        }
    }
    if (ex & kExAnim) {
        uint8_t n = r.u8();
        if (n == 0 || n >= sizeof(t.ownerAnim) || !r.need(n)) {
            r.ok = false;
            return;
        }
        memcpy(t.ownerAnim, r.p, n);
        t.ownerAnim[n] = 0;
        r.p += n;
        t.ownerAnimFrame = r.f();
        t.ownerAnimRate = r.f();
        t.ownerAnimFrameAt = st().frame;
    } else {
        t.ownerAnim[0] = 0;
    }
}

void skipExtras(Reader& r, uint8_t ex) {
    if (ex & kExRail) r.need(9) && (r.p += 9);
    if (ex & kExRailMover) r.need(22) && (r.p += 22);
    if (ex & kExRotator) r.need(26) && (r.p += 26);
    if (ex & kExRailRotator) r.need(18) && (r.p += 18);
    if (ex & kExAnim) {
        uint8_t n = r.u8();
        r.need(n + 8u) && (r.p += n + 8u);
    }
}

// Does the snapshot's sender run the actor (for us)?
bool acceptFrom(Tracked& t, uint8_t sender, bool claimed) {
    State& s = st();
    uint8_t me = netLocalId();
    if (t.busy) {
        return false;  // the local player's race or talk: his copy is the real one
    }
    if (t.claimUntil > s.frame) {
        // We touch it too: the lower player id keeps it.
        if (claimed && sender < me) {
            t.claimUntil = 0;
            t.owner = sender;
            t.remoteClaimUntil = s.frame + kClaimFrames;
            return true;
        }
        return false;
    }
    if (claimed) {
        t.owner = sender;
        t.remoteClaimUntil = s.frame + kClaimFrames;
        return true;
    }
    if (t.remoteClaimUntil > s.frame && t.owner != sender) {
        return false;  // someone touching it runs it
    }
    if (t.owner == sender) {
        return true;
    }
    if (t.owner == me) {
        // Both think they are nearest: the lower id keeps it.
        if (sender < me) {
            t.owner = sender;
            return true;
        }
        return false;
    }
    t.owner = sender;
    return true;
}

void applyMessage(const std::vector<uint8_t>& m) {
    State& s = st();
    Reader r{m.data() + 1, m.data() + m.size()};
    uint8_t sender = r.u8();
    uint32_t stage = r.u32();
    uint8_t scenario = r.u8();
    uint32_t senderFrame = r.u32();
    uint8_t count = r.u8();
    if (!r.ok || stage != s.stage || scenario != s.scenario || sender == netLocalId()) return;
    for (int i = 0; i < count && r.ok; i++) {
        uint32_t id = r.u32();
        uint32_t nerve = r.u32();
        uint16_t step = r.u16();
        uint8_t flags = r.u8();
        Pose p;
        for (int k = 0; k < 3; k++) p.pos[k] = r.f();
        for (int k = 0; k < 4; k++) p.q[k] = (float)(int16_t)r.u16() / 32767.0f;
        qnorm(p.q);
        uint8_t ex = r.u8();
        if (!r.ok) break;
        auto it = s.byId.find(id);
        Tracked* t = it != s.byId.end() ? &s.list[(size_t)it->second] : nullptr;
        if (t == nullptr || !acceptFrom(*t, sender, (flags & kFlagClaim) != 0) || !syncsTransform(*t)) {
            skipExtras(r, ex);
            continue;
        }
        s.recvEntries++;
        applyExtras(*t, r, ex);
        if (t->snaps > 0 && senderFrame <= t->last.senderFrame) {
            continue;  // out of order
        }
        t->prev = t->last;
        t->last.pose = p;
        t->last.senderFrame = senderFrame;
        t->last.recvFrame = s.frame;
        if (t->snaps < 2) t->snaps++;
        if (nerve != t->ownerNerve) t->nerveDirty = true;
        t->ownerNerve = nerve;
        t->ownerStep = step;
        t->ownerFlags = flags;
    }
}

// Where the owner's copy is now: the last state carried forward by its
// motion between the last two (a few frames: the message's age plus about
// half a round trip).
void targetPose(const Tracked& t, Pose* out) {
    State& s = st();
    *out = t.last.pose;
    if (t.snaps < 2 || t.last.senderFrame <= t.prev.senderFrame) return;
    float span = (float)(t.last.senderFrame - t.prev.senderFrame);
    if (span > 30.0f) return;
    float ahead = (float)(s.frame - t.last.recvFrame) + 3.0f;
    if (ahead > 12.0f) ahead = 12.0f;
    float k = ahead / span;
    float jump2 = dist2(t.last.pose.pos, t.prev.pose.pos);
    if (jump2 > 600.0f * 600.0f) return;  // a teleport, not motion
    for (int i = 0; i < 3; i++) out->pos[i] += (t.last.pose.pos[i] - t.prev.pose.pos[i]) * k;
    // Spin: delta = last * inverse(prev), raised to k.
    float inv[4] = {-t.prev.pose.q[0], -t.prev.pose.q[1], -t.prev.pose.q[2], t.prev.pose.q[3]};
    float d[4], dk[4];
    qmul(t.last.pose.q, inv, d);
    qpow(d, k, dk);
    qmul(dk, t.last.pose.q, out->q);
    qnorm(out->q);
}

void updateShown(Tracked& t) {
    Pose target;
    targetPose(t, &target);
    if (!t.shown || dist2(t.shownPose.pos, target.pos) > 800.0f * 800.0f) {
        t.shownPose = target;
        t.shown = true;
        return;
    }
    const float k = 0.35f;
    for (int i = 0; i < 3; i++) t.shownPose.pos[i] += (target.pos[i] - t.shownPose.pos[i]) * k;
    qblend(t.shownPose.q, target.q, k, t.shownPose.q);
}

}  // namespace

void actorStateOnMessage(const uint8_t* data, unsigned len) {
    if (len < kActorStateHeader || data[0] != kMsgActorState) return;
    PortHostAllocScope host;
    st().inbox.emplace_back(data, data + len);
}

}  // namespace net

using namespace net;

extern "C" void port_actor_sync_track(void* actorPtr, uint32_t id) {
    if (!enabled() || actorPtr == nullptr) return;
    State& s = st();
    PortHostAllocScope host;
    if (s.index.count(actorPtr) != 0) return;
    Tracked t;
    t.actor = static_cast<LiveActor*>(actorPtr);
    t.id = id;
    s.index[actorPtr] = (int)s.list.size();
    s.byId[id] = (int)s.list.size();
    s.list.push_back(t);
}

// MapParts constructors (decomp, TARGET_PC): kind 1 rail mover, 2 rotator,
// 3 rail rotator.
extern "C" void port_actor_sync_part(void* host, int kind, void* part) {
    if (!enabled() || host == nullptr) return;
    PortHostAllocScope h;
    st().parts.push_back({host, {kind, part}});
}

extern "C" void port_actor_sync_reset(void) {
    State& s = st();
    PortHostAllocScope host;
    if (logging() && s.live)
        port_log("actors: scene over: %u messages (%u states) sent, %u states applied", s.sentMsgs, s.sentEntries, s.recvEntries);
    s.list.clear();
    s.index.clear();
    s.byId.clear();
    s.parts.clear();
    s.inbox.clear();
    s.bosses.clear();
    s.live = false;
    s.standing = s.holding = s.lastHeld = nullptr;
    s.cursor = 0;
    s.sentMsgs = s.sentEntries = s.recvEntries = 0;
}

extern "C" int port_actor_sync_remote_owned(const void* nameObj) {
    if (!enabled() || nameObj == nullptr) return 0;
    Tracked* t = find(nameObj);
    return t != nullptr && driven(*t) ? 1 : 0;
}

extern "C" void port_actor_sync_message(void* actorPtr, void* senderActor) {
    if (!enabled() || !st().live || senderActor == nullptr) return;
    if (senderActor != MarioAccess::getPlayerActor()) return;
    if (Tracked* t = find(actorPtr)) {
        if (syncsTransform(*t)) claim(*t);
    }
}

// Bosses by placement object name: the big ones run their fight from a
// sequencer, so their own nerves say little.
static bool isBossObjName(const char* n) {
    static const char* const kBoss[] = {"KoopaVs", "DinoPackun", "TripodBoss", "TriPodBoss", "BossBegoman", "BossStinkBug", "BossKameck", "Dodoryu",
                                        "OtaKing", "Polta", "SkeletalFishBoss", "TombSpider", "IceMerameraKing", "KoopaJrShip"};
    for (const char* b : kBoss) {
        if (strncmp(n, b, strlen(b)) == 0) return true;
    }
    // Tests: PETARI_TEST_BOSS=<object name> treats that kind as a boss.
    static const char* test = getenv("PETARI_TEST_BOSS");
    return test != nullptr && *test != 0 && strcmp(n, test) == 0;
}

extern "C" void port_actor_sync_obj_name(void* actor, const char* objName) {
    if (!enabled() || actor == nullptr || objName == nullptr || !isBossObjName(objName)) return;
    PortHostAllocScope host;
    st().bosses[actor] = true;
    if (Tracked* t = find(actor)) {
        t->family = kFamBoss;
        t->familyKnown = true;
    }
}

extern "C" int port_actor_is_boss(void* actorPtr) {
    LiveActor* a = static_cast<LiveActor*>(actorPtr);
    if (a == nullptr) return 0;
    if (st().bosses.count(actorPtr) != 0) return 1;
    if (a->mSpine == nullptr) return 0;
    const NerveInfo* n = nerveInfo(a->mSpine->getCurrentNerve());
    return n != nullptr && n->family == kFamBoss ? 1 : 0;
}

extern "C" const char* port_actor_nerve_name(void* actorPtr, int* family) {
    LiveActor* a = static_cast<LiveActor*>(actorPtr);
    const NerveInfo* n = a != nullptr && a->mSpine != nullptr ? nerveInfo(a->mSpine->getCurrentNerve()) : nullptr;
    if (family) *family = n ? n->family : -1;
    return n ? n->name : (a && a->mSpine ? "(unregistered)" : "(no spine)");
}

extern "C" int port_actor_recently_held(void* actorPtr) {
    State& s = st();
    return actorPtr != nullptr && actorPtr == s.lastHeld && s.frame < s.lastHeldUntil ? 1 : 0;
}

extern "C" void port_actor_sync_dead(void* actorPtr) {
    // Nothing yet: the owner's dead flag goes out with its next state.
    (void)actorPtr;
}

extern "C" void port_actor_sync_movement(void* actorPtr) {
    State& s = st();
    if (!s.live) return;
    Tracked* t = find(actorPtr);
    if (t == nullptr || !driven(*t)) return;
    LiveActor* a = t->actor;
    updateShown(*t);
    a->mPosition.set(t->shownPose.pos[0], t->shownPose.pos[1], t->shownPose.pos[2]);

    // The owner's state machine.
    if (syncsNerve(*t) && a->mSpine != nullptr && t->ownerNerve != 0) {
        uint32_t local = nerveHashOf(a->mSpine);
        const NerveInfo* n = nerveByHash(t->ownerNerve);
        if (local == t->ownerNerve || n == nullptr || n->deny) {
            t->mismatchSince = 0;
            t->nerveDirty = false;
            if (local == t->ownerNerve && t->family != kFamEnemy) {
                // Timed states: keep the step (a countdown) in line.
                int32_t want = (int32_t)t->ownerStep + (int32_t)(s.frame - t->last.recvFrame) + 3;
                if (a->mSpine->mStep >= 0 && abs(a->mSpine->mStep - want) > 10) a->mSpine->mStep = want;
            }
        } else {
            const NerveInfo* cur = nerveInfo(a->mSpine->getCurrentNerve());
            if (t->mismatchSince == 0) t->mismatchSince = s.frame;
            bool curDenied = cur != nullptr && cur->deny;
            // The owner moved on (follow now), or the copy has been in another
            // state for a while (it got out of step: catch up).
            if (!curDenied && (t->nerveDirty || s.frame - t->mismatchSince > 45)) {
                if (logging()) port_log("actors: %s follows its owner: %s -> %s", a->mName, cur ? cur->name : "?", n->name);
                a->mSpine->setNerve(static_cast<const Nerve*>(n->nerve));
                t->nerveDirty = false;
                t->mismatchSince = 0;
            }
        }
    }
    if (t->ownerAnim[0] != 0 && a->mModelManager != nullptr) {
        // The owner's animation, at its frame.
        const char* local = MR::getPlayingBckName(a);
        if ((local == nullptr || strcmp(local, t->ownerAnim) != 0) && MR::isExistBck(a, t->ownerAnim)) {
            MR::startBck(a, t->ownerAnim);
            local = MR::getPlayingBckName(a);
        }
        if (local != nullptr && strcmp(local, t->ownerAnim) == 0) {
            float want = t->ownerAnimFrame + t->ownerAnimRate * (float)(s.frame - t->ownerAnimFrameAt + 3);
            float end = MR::getBckFrameMax(a);
            if (end > 0.0f && want >= end) want = MR::isBckLooped(a) ? fmodf(want, end) : end;
            float now = MR::getBckFrame(a);
            if (fabsf(now - want) > 3.0f && fabsf(now - want) < end - 3.0f) MR::setBckFrame(a, want);
        }
    }
    if (syncsFlags(*t)) {
        bool hidden = (t->ownerFlags & kFlagHidden) != 0;
        if (hidden != MR::isHiddenModel(a)) {
            if (hidden) MR::hideModel(a);
            else MR::showModel(a);
        }
        if (a->mCollisionParts != nullptr) {
            bool valid = (t->ownerFlags & kFlagCollision) != 0;
            if (valid != MR::isValidCollisionParts(a)) {
                if (valid) MR::validateCollisionParts(a);
                else MR::invalidateCollisionParts(a);
            }
        }
    }
}

extern "C" int port_actor_sync_driven(void* actorPtr, const char** anim) {
    State& s = st();
    Tracked* t = s.live ? find(actorPtr) : nullptr;
    if (t == nullptr || !driven(*t)) return 0;
    if (anim != nullptr) *anim = t->ownerAnim;
    return 1;
}

extern "C" void port_actor_sync_mtx(void* actorPtr) {
    State& s = st();
    if (!s.live) return;
    Tracked* t = find(actorPtr);
    if (t == nullptr || !driven(*t) || !t->shown) return;
    if (t->actor->getBaseMtx() == nullptr) return;
    TPos3f m;
    poseToMtx(t->shownPose, m.mMtx);
    MR::setBaseTRMtx(t->actor, m);
}

extern "C" void port_actor_sync_update(uint32_t stage, uint8_t scenario) {
    if (!enabled()) return;
    State& s = st();
    PortHostAllocScope host;
    if (!s.live || s.stage != stage || s.scenario != scenario) {
        s.stage = stage;
        s.scenario = scenario;
        s.live = true;
        // The parts made at scene set-up, matched to their hosts.
        for (auto& p : s.parts) {
            Tracked* t = find(p.first);
            if (t == nullptr) continue;
            if (p.second.first == 1) t->railMover = static_cast<MapPartsRailMover*>(p.second.second);
            else if (p.second.first == 2) t->rotator = static_cast<MapPartsRotator*>(p.second.second);
            else if (p.second.first == 3) t->railRotator = static_cast<MapPartsRailRotator*>(p.second.second);
        }
        s.parts.clear();
        for (Tracked& t : s.list) {
            t.enemySensor = port_world_has_enemy_sensor(t.actor) != 0;
            t.excluded = port_world_is_item(t.actor) != 0;
        }
        if (logging()) port_log("actors: %d placed actors tracked", (int)s.list.size());
    }
    s.frame++;
    uint8_t me = netLocalId();

    // Received states.
    if (!s.inbox.empty()) {
        std::vector<std::vector<uint8_t>> in;
        in.swap(s.inbox);
        for (auto& m : in) applyMessage(m);
    }

    // What the local player touches without a message: stands on, holds.
    MarioActor* mario = MarioAccess::getPlayerActor();
    s.standing = nullptr;
    s.holding = nullptr;
    if (mario != nullptr) {
        HitSensor* taking = MarioAccess::getTakingSensor();
        if (taking != nullptr) {
            s.holding = taking->mHost;
            s.lastHeld = taking->mHost;
            s.lastHeldUntil = s.frame + 300;  // thrown: its hits are the player's for 5 s
        }
    }

    Player players[1 + kMaxRemotes];
    int np = playersHere(players, 1 + kMaxRemotes);
    bool alone = np == 1;
    bool reassign = s.frame % 10 == 0;
    auto present = [&](uint8_t id) {
        for (int i = 0; i < np; i++) {
            if (players[i].id == id) return true;
        }
        return false;
    };

    for (Tracked& t : s.list) {
        LiveActor* a = t.actor;
        learnFamily(t);
        if (!syncsTransform(t)) continue;
        if (driven(t) && (t.ownerFlags & kFlagDead) != 0 && !MR::isDead(a) && syncsFlags(t) && !t.enemySensor) {
            // Broken or gone where its owner runs it (a crate, a rock): here
            // too, without its loot (the owner's drop brings it).
            if (logging()) port_log("actors: %s is gone for its owner: gone here too", a->mName);
            port_world_remote_kill(a);
            t.killedByOwner = true;
            continue;
        }
        if (t.killedByOwner && driven(t) && (t.ownerFlags & kFlagDead) == 0 && MR::isDead(a)) {
            // ... and back (a platform that comes back).
            if (logging()) port_log("actors: %s is back for its owner: back here too", a->mName);
            t.killedByOwner = false;
            a->appear();
            continue;
        }
        if (alone) {
            t.owner = me;
            continue;
        }
        if (t.owner != 0 && !present(t.owner)) {
            // Its owner left the stage: whoever is nearest takes it over.
            t.owner = 0;
            t.remoteClaimUntil = 0;
            t.snaps = 0;
            t.shown = false;
        }
        float ap[3] = {a->mPosition.x, a->mPosition.y, a->mPosition.z};
        bool close = dist2(players[0].pos, ap) < 5000.0f * 5000.0f;
        if (t.family == kFamNpc && a->mSpine != nullptr) {
            const NerveInfo* n = nerveInfo(a->mSpine->getCurrentNerve());
            bool busy = close && n != nullptr && busyNpcNerve(n->name);
            if (busy != t.busy && logging()) port_log("actors: %s %s with the local player", a->mName, busy ? "busy" : "no longer busy");
            t.busy = busy;
            if (busy) {
                claim(t);
                continue;
            }
        }
        if (!MR::isDead(a) && (a == s.holding || (close && mario != nullptr && MarioAccess::isOnActor(a)))) {
            claim(t);
            continue;
        }
        if (!reassign || t.claimUntil > s.frame || t.remoteClaimUntil > s.frame) continue;
        // Nearest player, with a margin before it changes hands.
        int best = 0, cur = -1;
        float bestD = 1e30f;
        for (int i = 0; i < np; i++) {
            float d = dist2(ap, players[i].pos);
            if (d < bestD) {
                bestD = d;
                best = i;
            }
            if (players[i].id == t.owner) cur = i;
        }
        if (cur < 0) {
            t.owner = players[best].id;
        } else if (best != cur) {
            float dc = sqrtf(dist2(ap, players[cur].pos));
            if (sqrtf(bestD) + kHandoverMargin < dc) t.owner = players[best].id;
        }
        if (t.owner == me && t.snaps > 0) {
            // Taking over a copy: it carries on from where the owner had it.
            t.snaps = 0;
            t.shown = false;
        }
    }

    if (logging() && s.frame % 600 == 0) {
        port_log("actors: %u messages (%u states) sent, %u states applied so far", s.sentMsgs, s.sentEntries, s.recvEntries);
    }
    if (logging() && s.frame % 30 == 0) {
        // Moving actors near the local player, to compare between clients.
        int n = 0;
        for (Tracked& t : s.list) {
            if (!t.dynamic && t.snaps == 0) continue;
            MtxPtr m = t.actor->getBaseMtx();
            if (m == nullptr || MR::isDead(t.actor)) continue;
            float p[3] = {m[0][3], m[1][3], m[2][3]};
            if (dist2(p, players[0].pos) > 6000.0f * 6000.0f) continue;
            const NerveInfo* ni = t.actor->mSpine ? nerveInfo(t.actor->mSpine->getCurrentNerve()) : nullptr;
            Pose q;
            mtxToPose(m, &q);
            port_log("actors: t=%lld %08x %s at %.0f %.0f %.0f owner %u%s nerve %s q %.4f %.4f %.4f %.4f", (long long)(port_host_time_ns() / 1000000),
                     t.id, t.actor->mName, p[0], p[1], p[2], t.owner, driven(t) ? " (driven)" : "", ni ? ni->name : "-", q.q[0], q.q[1], q.q[2], q.q[3]);
            if (++n == 40) break;
        }
    }

    // Send what we run and what changed.
    if (alone || s.frame % kSendEvery != 0) return;
    struct Due {
        Tracked* t;
        Pose pose;
        uint8_t flags;
        uint32_t age;
    };
    std::vector<Due> due;
    const float* mePos = players[0].pos;
    for (Tracked& t : s.list) {
        if (!syncsTransform(t) || t.owner != me) continue;
        Pose p;
        if (!currentPose(t, &p)) continue;
        uint8_t f = flagsOf(t);
        if (!t.haveBase) {
            t.base = p;
            t.haveBase = true;
        }
        uint32_t nerve = nerveHashOf(t.actor->mSpine);
        if (!t.dynamic) {
            // Sent once it moves or changes state: a still one needs nothing.
            bool moved = dist2(p.pos, t.base.pos) > 1.0f || fabsf(qdot(p.q, t.base.q)) < 0.99999f;
            if (!t.haveBaseState) {
                t.baseNerve = nerve;
                t.baseFlags = f;
                t.haveBaseState = true;
            }
            if (!moved && nerve == t.baseNerve && f == t.baseFlags) continue;
            t.dynamic = true;
        }
        // A new animation counts as a change: an NPC that stands still and
        // starts talking (BarneyNPC) shows it on the others now, not in 2 s.
        const char* anim = t.actor->mModelManager != nullptr ? MR::getPlayingBckName(t.actor) : nullptr;
        uint32_t animHash = anim != nullptr ? fnvStr(anim) : 0;
        bool changed = !t.everSent || dist2(p.pos, t.sent.pos) > 0.25f || fabsf(qdot(p.q, t.sent.q)) < 0.99999f || nerve != t.sentNerve ||
                       f != t.sentFlags || animHash != t.sentAnim;
        uint32_t age = s.frame - t.sentFrame;
        bool near = (f & kFlagClaim) != 0 || dist2(p.pos, mePos) < kNear * kNear;
        uint32_t every = changed ? (near ? (uint32_t)kSendEvery : 12u) : 120u;
        if (age < every) continue;
        due.push_back({&t, p, f, age});
    }
    if (due.empty()) return;
    std::sort(due.begin(), due.end(), [](const Due& x, const Due& y) { return x.age > y.age; });
    std::vector<uint8_t> b;
    b.reserve(kMaxMessage + 128);
    b.push_back(kMsgActorState);
    b.push_back(me);
    put32(b, stage);
    b.push_back(scenario);
    put32(b, s.frame);
    b.push_back(0);  // count
    int count = 0;
    for (Due& d : due) {
        if (b.size() + 140 > kMaxMessage || count == 255) break;
        writeEntry(b, *d.t, d.pose, d.flags);
        d.t->everSent = true;
        d.t->sent = d.pose;
        d.t->sentNerve = nerveHashOf(d.t->actor->mSpine);
        d.t->sentFlags = d.flags;
        d.t->sentFrame = s.frame;
        const char* sentAnim = d.t->actor->mModelManager != nullptr ? MR::getPlayingBckName(d.t->actor) : nullptr;
        d.t->sentAnim = sentAnim != nullptr ? fnvStr(sentAnim) : 0;
        count++;
    }
    b[kActorStateHeader - 1] = (uint8_t)count;
    netSendRaw(b.data(), b.size());
    s.sentMsgs++;
    s.sentEntries += (uint32_t)count;
}
