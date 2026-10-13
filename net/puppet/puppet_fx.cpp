// See puppet_fx.h.
#include "net/puppet/puppet_fx.h"

#include <stdlib.h>
#include <string.h>

#include <deque>
#include <vector>

#include "Game/Effect/MultiEmitter.hpp"
#include "Game/LiveActor/EffectKeeper.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "net/client/net_client.h"
#include "port/heap_routing.h"
#include "port/port.h"

namespace net {
namespace {

enum : uint8_t { kOpPlay = 1, kOpStop = 2, kOpForceStop = 3 };
enum : uint8_t { kHasTrans = 1, kHasRot = 2, kHasScale = 4, kFollowOff = 8 };

struct Fx {
    uint8_t op;
    uint8_t mask;
    uint16_t index;
    uint16_t hash;  // the emitter's name hash as played (renamed, floor variant)
    float trans[3];
    float rot[3];
    float scale;
    int64_t atNs;  // when it was played (loopback) or arrived
    char name[48];  // the name the game played it by ("" if too long)
};

constexpr size_t kMaxPerMessage = 16;  // 3 + 16 * 80 bytes at most: under the relay's 1400

std::vector<Fx> sOut;                       // the local player's, this frame
std::deque<Fx> sIn[256];                    // by remote player id (0: loopback)
bool sApplying = false;                     // a puppet is playing one (not the local player's)
const MultiEmitter* sLastPlayed = nullptr;  // the emitter of sOut.back(), for port_net_fx_transform

bool isLocalPlayer(LiveActor* pActor) {
    return !sApplying && pActor != nullptr && pActor == static_cast< LiveActor* >(MarioAccess::getPlayerActor());
}

s32 emitterIndex(const EffectKeeper* pKeeper, const MultiEmitter* pEmitter) {
    for (s32 i = 0; i < pKeeper->_C.size(); i++) {
        if (pKeeper->getEmitter(i) == pEmitter) {
            return i;
        }
    }
    return -1;
}

// The puppet's emitter for one the sender played.  The same emitter is the
// one with the same name hash (as played: MarioActor's renames, the floor
// variant of an attribute effect), first at the same index (MarioActor's
// effect group, built in the same order), else anywhere (the effects
// MarioActor adds after the group).  An emitter MarioActor renamed keeps its
// original name on the puppet: then the index alone; the name is the last
// resort.
MultiEmitter* findEmitter(const EffectKeeper* pKeeper, const Fx& f) {
    s32 count = pKeeper->_C.size();
    if (f.index < count && pKeeper->getEmitter((s32)f.index)->isEqualName(f.hash)) {
        return pKeeper->getEmitter((s32)f.index);
    }
    for (s32 i = 0; i < count; i++) {
        if (pKeeper->getEmitter(i)->isEqualName(f.hash)) {
            return pKeeper->getEmitter(i);
        }
    }
    if (f.index < count) {
        return pKeeper->getEmitter((s32)f.index);
    }
    if (f.name[0] != 0 && pKeeper->isRegisteredEmitter(f.name)) {
        return pKeeper->getEmitter(f.name);
    }
    return nullptr;
}

void putF(std::vector<uint8_t>& b, const float* v, int n) {
    const uint8_t* p = reinterpret_cast< const uint8_t* >(v);
    b.insert(b.end(), p, p + 4 * n);
}

}  // namespace

void fxSend() {
    if (sOut.empty()) {
        return;
    }
    PortHostAllocScope hostAlloc;
    if (!netEnabled()) {
        // PETARI_LOOPBACK: the copy of the local player plays them.
        for (Fx f : sOut) {
            f.atNs = port_host_time_ns();
            if (sIn[0].size() < 256) sIn[0].push_back(f);
        }
        sOut.clear();
        return;
    }
    uint8_t you = netLocalId();
    for (size_t start = 0; you != 0 && start < sOut.size(); start += kMaxPerMessage) {
        size_t n = sOut.size() - start < kMaxPerMessage ? sOut.size() - start : kMaxPerMessage;
        std::vector<uint8_t> b = {kMsgPlayerEffect, you, (uint8_t)n};
        for (size_t i = start; i < start + n; i++) {
            const Fx& f = sOut[i];
            b.push_back(f.op);
            b.push_back((uint8_t)f.index);
            b.push_back((uint8_t)(f.index >> 8));
            b.push_back((uint8_t)f.hash);
            b.push_back((uint8_t)(f.hash >> 8));
            b.push_back(f.mask);
            uint8_t nameLen = (uint8_t)strlen(f.name);
            b.push_back(nameLen);
            b.insert(b.end(), f.name, f.name + nameLen);
            if (f.mask & kHasTrans) putF(b, f.trans, 3);
            if (f.mask & kHasRot) putF(b, f.rot, 3);
            if (f.mask & kHasScale) putF(b, &f.scale, 1);
        }
        netSendRaw(b.data(), b.size());
    }
    sOut.clear();
}

void fxOnMessage(const uint8_t* m, size_t len) {
    if (len < 3 || m[0] != kMsgPlayerEffect) return;
    PortHostAllocScope hostAlloc;
    std::deque<Fx>& q = sIn[m[1]];
    size_t o = 3;
    for (uint8_t i = 0; i < m[2]; i++) {
        if (o + 7 > len) return;
        Fx f = {};
        f.atNs = port_host_time_ns();
        f.op = m[o];
        f.index = (uint16_t)(m[o + 1] | m[o + 2] << 8);
        f.hash = (uint16_t)(m[o + 3] | m[o + 4] << 8);
        f.mask = m[o + 5];
        size_t nameLen = m[o + 6];
        o += 7;
        if (o + nameLen > len) return;
        if (nameLen < sizeof(f.name)) memcpy(f.name, m + o, nameLen);
        o += nameLen;
        size_t need = (f.mask & kHasTrans ? 12 : 0) + (f.mask & kHasRot ? 12 : 0) + (f.mask & kHasScale ? 4 : 0);
        if (o + need > len) return;
        if (f.mask & kHasTrans) { memcpy(f.trans, m + o, 12); o += 12; }
        if (f.mask & kHasRot) { memcpy(f.rot, m + o, 12); o += 12; }
        if (f.mask & kHasScale) { memcpy(&f.scale, m + o, 4); o += 4; }
        if (q.size() < 256) q.push_back(f);  // a hidden puppet's pile-up is capped
    }
}

void fxApply(uint8_t remoteId, LiveActor* pPuppet, int64_t delayNs) {
    std::deque<Fx>& q = sIn[remoteId];
    if (q.empty()) return;
    PortHostAllocScope hostAlloc;
    EffectKeeper* keeper = pPuppet != nullptr ? pPuppet->mEffectKeeper : nullptr;
    int64_t due = port_host_time_ns() - delayNs;
    sApplying = true;
    while (!q.empty()) {
        if (keeper != nullptr && q.front().atNs > due) {
            break;  // the puppet is drawn this far behind: so are its effects
        }
        Fx f = q.front();
        q.pop_front();
        if (keeper == nullptr) {
            continue;  // not shown here
        }
        MultiEmitter* e = findEmitter(keeper, f);
        if (e == nullptr) {
            continue;  // an effect the puppet does not have
        }
        static const bool sLog = getenv("PETARI_FXLOG") != nullptr;  // debug: what puppets play
        if (sLog) {
            s32 got = e != nullptr ? emitterIndex(keeper, e) : -1;
            port_log("fx: player %u op %u emitter %u (%04x) -> %d%s", remoteId, f.op, f.index, f.hash, (int)got, got == (s32)f.index ? "" : " (moved)");
        }
        if (f.op == kOpPlay) {
            e->createEmitter();
            if (f.mask & kFollowOff) e->forceFollowOff();
            if (f.mask & kHasScale) e->setGlobalScale(TVec3f(f.scale, f.scale, f.scale), -1);
            if (f.mask & kHasTrans) e->setGlobalTranslation(TVec3f(f.trans[0], f.trans[1], f.trans[2]), -1);
            if (f.mask & kHasRot) e->setGlobalRotationDegree(TVec3f(f.rot[0], f.rot[1], f.rot[2]), -1);
        } else if (f.op == kOpStop) {
            e->deleteEmitter();
        } else if (f.op == kOpForceStop) {
            e->forceDeleteEmitter();
        }
    }
    sApplying = false;
}

void fxStopAll(LiveActor* pPuppet) {
    if (pPuppet != nullptr && pPuppet->mEffectKeeper != nullptr) {
        sApplying = true;
        pPuppet->mEffectKeeper->deleteEmitterAll();
        sApplying = false;
    }
}

}  // namespace net

namespace net {
namespace {
void note(LiveActor* pActor, const MultiEmitter* pEmitter, const char* pName, int op) {
    s32 index = emitterIndex(pActor->mEffectKeeper, pEmitter);
    if (index < 0) {
        return;
    }
    PortHostAllocScope hostAlloc;
    if (sOut.size() >= 256) {
        return;
    }
    Fx f = {};
    f.op = (uint8_t)op;
    f.index = (uint16_t)index;
    f.hash = pEmitter->mHash;
    if (pName != nullptr && strlen(pName) < sizeof(f.name)) {
        strcpy(f.name, pName);
    }
    sOut.push_back(f);
    sLastPlayed = op == kOpPlay ? pEmitter : nullptr;
}
}  // namespace
}  // namespace net

extern "C" void port_net_fx_note(LiveActor* pActor, const char* pName, int op) {
    if (!net::isLocalPlayer(pActor) || pActor->mEffectKeeper == nullptr || !pActor->mEffectKeeper->isRegisteredEmitter(pName)) {
        return;
    }
    net::note(pActor, pActor->mEffectKeeper->getEmitter(pName), pName, op);
}

// An emitter the game starts or stops directly rather than by name
// (MarioActor::stopCommonEffect: the running dust and other loops).
extern "C" void port_net_fx_emitter(LiveActor* pActor, const void* pEmitter, int op) {
    if (!net::isLocalPlayer(pActor) || pActor->mEffectKeeper == nullptr || pEmitter == nullptr) {
        return;
    }
    net::note(pActor, static_cast< const MultiEmitter* >(pEmitter), nullptr, op);
}

// MarioActor::playEffectTrans / RT / RTZ / SRT, after their playEffect: the
// emitter just queued gets where the game put it.
extern "C" void port_net_fx_transform(LiveActor* pActor, const void* pEmitter, const float* pTrans, const float* pRotDeg, float scale, int followOff) {
    // Only the emitter just played: playEffect can also hand back one that
    // was already running (a floor-material effect), which was not sent.
    if (!net::isLocalPlayer(pActor) || pEmitter == nullptr || pEmitter != net::sLastPlayed || net::sOut.empty()) {
        return;
    }
    net::sLastPlayed = nullptr;
    net::Fx& f = net::sOut.back();
    if (pTrans != nullptr) {
        memcpy(f.trans, pTrans, 12);
        f.mask |= net::kHasTrans;
    }
    if (pRotDeg != nullptr) {
        memcpy(f.rot, pRotDeg, 12);
        f.mask |= net::kHasRot;
    }
    if (scale > 0.0f) {
        f.scale = scale;
        f.mask |= net::kHasScale;
    }
    if (followOff) {
        f.mask |= net::kFollowOff;
    }
}
