// See puppet_sound.h.
#include "net/puppet/puppet_sound.h"

#include <stdlib.h>
#include <string.h>

#include <deque>
#include <vector>

#include "Game/AudioLib/AudAnmSoundObject.hpp"
#include "Game/AudioLib/AudSoundObject.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "net/client/net_client.h"
#include "port/heap_routing.h"
#include "port/port.h"
#include <JSystem/JAudio2/JAISound.hpp>

namespace net {
namespace {

enum : uint8_t { kOpSound = 1, kOpLevel = 2, kOpAnim = 3, kOpStop = 4 };
enum : uint8_t { kHasParams = 1, kHasSoundParams = 2 };

struct Snd {
    uint8_t op;
    uint8_t mapCode;
    uint8_t mask;
    uint32_t id;
    int32_t a, b;   // startSoundParam's two; a stop's fade
    float sp[5];    // volume, fx mix, pitch, pan, dolby
    int64_t atNs;   // when it was played (loopback) or arrived
};

constexpr size_t kMaxPerMessage = 32;     // 3 + 32 * 39 bytes at most: under the relay's 1400
constexpr uint32_t kLevelLifeFrames = 4;  // a level sound outlives a late batch (two frames per batch, plus jitter)
constexpr uint32_t kGroupPlayerVoice = 1;  // SE_PLAYER_VOICE (AudSoundObject.hpp)

std::vector<Snd> sOut;             // the local player's, since the last send
std::deque<Snd> sIn[256];          // by remote player id (0: loopback)
const void* sLocalObject = nullptr;  // the local MarioActor's sound object
uint32_t sFrame = 0;

bool isLocal(const void* pObject) {
    return pObject != nullptr && pObject == sLocalObject;
}

void queue(const Snd& s) {
    PortHostAllocScope hostAlloc;
    if (s.op == kOpLevel) {
        // Called every frame while it lasts: once per batch is enough.
        for (const Snd& q : sOut) {
            if (q.op == kOpLevel && q.id == s.id) {
                return;
            }
        }
    }
    if (sOut.size() < 256) {
        sOut.push_back(s);
    }
}

uint8_t mapCodeOf(const void* pObject) {
    s32 code = static_cast< const AudSoundObject* >(pObject)->getMapCode();
    return (uint8_t)(code < 0 ? 0 : code > 255 ? 255 : code);
}

void put32(std::vector<uint8_t>& b, const void* v) {
    const uint8_t* p = static_cast< const uint8_t* >(v);
    b.insert(b.end(), p, p + 4);
}

}  // namespace

void sndSend() {
    sLocalObject = MR::isExistMario() ? MarioAccess::getPlayerActor()->mSoundObject : nullptr;
    // Every second frame: the relay's per-client message budget
    // (server/src/relay.ts RATE_LIMIT_PER_S) has PlayerState, ActorState and
    // PlayerEffect in it too.
    if (sOut.empty() || (++sFrame & 1) != 0) {
        return;
    }
    PortHostAllocScope hostAlloc;
    if (!netEnabled()) {
        // PETARI_LOOPBACK: the copy of the local player plays them.
        for (Snd s : sOut) {
            s.atNs = port_host_time_ns();
            if (sIn[0].size() < 256) sIn[0].push_back(s);
        }
        sOut.clear();
        return;
    }
    uint8_t you = netLocalId();
    for (size_t start = 0; you != 0 && start < sOut.size(); start += kMaxPerMessage) {
        size_t n = sOut.size() - start < kMaxPerMessage ? sOut.size() - start : kMaxPerMessage;
        std::vector<uint8_t> b = {kMsgPlayerSound, you, (uint8_t)n};
        for (size_t i = start; i < start + n; i++) {
            const Snd& s = sOut[i];
            b.push_back(s.op);
            put32(b, &s.id);
            b.push_back(s.mapCode);
            b.push_back(s.mask);
            if (s.mask & kHasParams) {
                put32(b, &s.a);
                put32(b, &s.b);
            }
            if (s.mask & kHasSoundParams) {
                for (int k = 0; k < 5; k++) put32(b, &s.sp[k]);
            }
        }
        netSendRaw(b.data(), b.size());
    }
    sOut.clear();
}

void sndOnMessage(const uint8_t* m, size_t len) {
    if (len < 3 || m[0] != kMsgPlayerSound) return;
    PortHostAllocScope hostAlloc;
    std::deque<Snd>& q = sIn[m[1]];
    size_t o = 3;
    for (uint8_t i = 0; i < m[2]; i++) {
        if (o + 7 > len) return;
        Snd s = {};
        s.atNs = port_host_time_ns();
        s.op = m[o];
        memcpy(&s.id, m + o + 1, 4);
        s.mapCode = m[o + 5];
        s.mask = m[o + 6];
        o += 7;
        size_t need = (s.mask & kHasParams ? 8 : 0) + (s.mask & kHasSoundParams ? 20 : 0);
        if (o + need > len) return;
        if (s.mask & kHasParams) {
            memcpy(&s.a, m + o, 4);
            memcpy(&s.b, m + o + 4, 4);
            o += 8;
        }
        if (s.mask & kHasSoundParams) {
            memcpy(s.sp, m + o, 20);
            o += 20;
        }
        if (q.size() < 256) q.push_back(s);  // a hidden puppet's pile-up is capped
    }
}

void sndApply(uint8_t remoteId, LiveActor* pPuppet, bool luigi, int64_t delayNs) {
    std::deque<Snd>& q = sIn[remoteId];
    if (q.empty()) return;
    PortHostAllocScope hostAlloc;
    AudAnmSoundObject* obj = pPuppet != nullptr ? pPuppet->mSoundObject : nullptr;
    int64_t due = port_host_time_ns() - delayNs;
    static const bool sLog = getenv("PETARI_SNDLOG") != nullptr;  // debug: what puppets play
    while (!q.empty()) {
        if (obj != nullptr && q.front().atNs > due) {
            break;  // the puppet is drawn this far behind: so are its sounds
        }
        Snd s = q.front();
        q.pop_front();
        if (obj == nullptr) {
            continue;  // not shown here
        }
        JAISoundID id(s.id);
        if (s.op == kOpStop) {
            if (sLog) port_log("snd: player %u stop %08x", remoteId, s.id);
            obj->stopSound(id, (u32)s.a);
            continue;
        }
        // The sender's floor (footstep and landing material), read by
        // setMapCodeToPort as the sound starts.
        obj->setMapCode(s.mapCode);
        JAISoundHandle* h = nullptr;
        if (s.op == kOpSound) {
            h = (s.mask & kHasParams) ? obj->startSoundParam(id, s.a, s.b) : obj->startSound(id);
        } else if (s.op == kOpLevel) {
            h = (s.mask & kHasParams) ? obj->startLevelSoundParam(id, s.a, s.b) : obj->startLevelSound(id);
            if (h != nullptr && h->isSoundAttached()) {
                h->getSound()->updateLifeTime(kLevelLifeFrames);
            }
        } else if (s.op == kOpAnim) {
            h = obj->startSound(id);
            if (h != nullptr && h->isSoundAttached() && (s.mask & kHasSoundParams)) {
                JASSoundParams& p = h->getSound()->getAuxiliary().mParams;
                p.mVolume = s.sp[0];
                p.mFxMix = s.sp[1];
                p.mPitch = s.sp[2];
                p.mPan = s.sp[3];
                p.mDolby = s.sp[4];
            }
        }
        if (sLog) {
            port_log("snd: player %u op %u id %08x map %u at %.0f %.0f %.0f: %s", remoteId, s.op, s.id, s.mapCode, pPuppet->mPosition.x,
                     pPuppet->mPosition.y, pPuppet->mPosition.z, h != nullptr && h->isSoundAttached() ? "playing" : "not started");
        }
        // AudSystem::switchToLuigi gave a voice the local player's
        // character; the puppet speaks with its own.
        if (h != nullptr && h->isSoundAttached() && id.getGroupID() == kGroupPlayerVoice && luigi != MR::isPlayerLuigi()) {
            obj->writePort(h, 12, luigi ? 20 : 0);
        }
    }
}

}  // namespace net

extern "C" void port_net_snd_note(const void* pObject, uint32_t soundId, int op) {
    if (!net::isLocal(pObject) || soundId == 0xFFFFFFFF) {
        return;
    }
    net::Snd s = {};
    s.op = (uint8_t)op;
    s.id = soundId;
    s.mapCode = net::mapCodeOf(pObject);
    net::queue(s);
}

// AudSoundObject::startSoundParam / startLevelSoundParam, after the start
// they wrap: the sound just queued gets its two parameters.
extern "C" void port_net_snd_params(const void* pObject, uint32_t soundId, int32_t a, int32_t b) {
    if (!net::isLocal(pObject) || net::sOut.empty() || net::sOut.back().id != soundId) {
        return;
    }
    net::Snd& s = net::sOut.back();
    s.mask |= net::kHasParams;
    s.a = a;
    s.b = b;
}

extern "C" void port_net_snd_anim(const void* pObject, uint32_t soundId, const float* pParams) {
    if (!net::isLocal(pObject)) {
        return;
    }
    net::Snd s = {};
    s.op = net::kOpAnim;
    s.id = soundId;
    s.mapCode = net::mapCodeOf(pObject);
    if (pParams != nullptr) {
        s.mask = net::kHasSoundParams;
        memcpy(s.sp, pParams, sizeof(s.sp));
    }
    net::queue(s);
}

extern "C" void port_net_snd_stop(const void* pObject, uint32_t soundId, uint32_t fade) {
    if (!net::isLocal(pObject)) {
        return;
    }
    net::Snd s = {};
    s.op = net::kOpStop;
    s.id = soundId;
    s.a = (int32_t)fade;
    s.mask = net::kHasParams;
    net::queue(s);
}
