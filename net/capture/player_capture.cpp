// Local player capture (P2-04).  Runs on the game thread inside
// GameScene::update; the ring is read from other threads under a lock.
#include "player_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mutex>

#include "Game/Animation/XanimePlayer.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "port/port.h"

namespace {

const int kRing = 256;  // ~4 s at 60 fps
NetPlayerCapture sRing[kRing];
uint32_t sCount = 0;  // captures written
std::mutex sLock;
char sAnimName[64];

// Last stage name hashed, by text: the game reuses the name's buffer across
// stage changes, so the pointer alone stays the same.
char sStageName[64] = {};
uint32_t sStageHash = 0;

}  // namespace

extern "C" void port_net_capture_player(void) {
    MarioActor* actor = MarioAccess::getPlayerActor();
    if (actor == nullptr) {
        return;
    }
    NetPlayerCapture c;
    memset(&c, 0, sizeof(c));
    c.timeNs = port_host_time_ns();

    const TVec3f* p = MR::getPlayerPos();
    c.pos[0] = p->x, c.pos[1] = p->y, c.pos[2] = p->z;
    const TVec3f* v = MR::getPlayerVelocity();
    c.vel[0] = v->x, c.vel[1] = v->y, c.vel[2] = v->z;
    TVec3f up, front;
    MR::getPlayerUpVec(&up);
    MR::getPlayerFrontVec(&front);
    c.up[0] = up.x, c.up[1] = up.y, c.up[2] = up.z;
    c.front[0] = front.x, c.front[1] = front.y, c.front[2] = front.z;
    if (MtxPtr m = MarioAccess::getBaseMtx()) {
        memcpy(c.baseMtx, m, sizeof(c.baseMtx));
    }

    const char* anim = nullptr;
    if (actor->mMarioAnim != nullptr) {
        XanimePlayer* xp = actor->mMarioAnim->getXanimePlayer();
        if (xp != nullptr) {
            anim = xp->getCurrentAnimationName();
            c.animFrame = xp->tellAnimationFrame();
            c.animRate = xp->getRate();
            for (int i = 0; i < 4; i++) {
                c.animWeights[i] = xp->mWeights[i];
            }
        }
    }
    c.animHash = anim != nullptr ? MR::getHashCode(anim) : 0;

    const char* stage = MR::getCurrentStageName();
    if (stage == nullptr) {
        sStageName[0] = 0;
        sStageHash = 0;
    } else if (strncmp(stage, sStageName, sizeof(sStageName)) != 0) {
        snprintf(sStageName, sizeof(sStageName), "%s", stage);
        sStageHash = MR::getHashCode(stage);
    }
    c.stageHash = sStageHash;
    c.scenario = MR::getCurrentScenarioNo();

    if (MR::isPlayerLuigi()) c.flags |= NET_CAPTURE_LUIGI;
    if (MR::isOnGroundPlayer()) c.flags |= NET_CAPTURE_GROUNDED;
    if (MR::isPlayerHidden()) c.flags |= NET_CAPTURE_HIDDEN;
    if (MR::isDemoActive()) c.flags |= NET_CAPTURE_IN_DEMO;
    c.flags |= (uint16_t)((actor->_B6A & 3) << NET_CAPTURE_EYELID_SHIFT);
    c.flags |= (uint16_t)((actor->mPlayerMode & 15) << NET_CAPTURE_MODE_SHIFT);

    std::lock_guard<std::mutex> lock(sLock);
    c.frame = sCount;
    sRing[sCount % kRing] = c;
    sCount++;
    snprintf(sAnimName, sizeof(sAnimName), "%s", anim != nullptr ? anim : "");

    // PETARI_CAPLOG=1: one line a second (positions, animation hash/frame).
    static const bool capLog = getenv("PETARI_CAPLOG") != nullptr;
    if (capLog && c.frame % 60 == 0) {
        port_log("capture: frame %u pos %.1f %.1f %.1f up %.2f %.2f %.2f anim %08x @%.1f x%.2f stage %08x/%d flags %x", c.frame, c.pos[0],
                 c.pos[1], c.pos[2], c.up[0], c.up[1], c.up[2], c.animHash, c.animFrame, c.animRate, c.stageHash, (int)c.scenario, c.flags);
    }
}

extern "C" int net_capture_latest(NetPlayerCapture* out) {
    std::lock_guard<std::mutex> lock(sLock);
    if (sCount == 0) {
        return 0;
    }
    *out = sRing[(sCount - 1) % kRing];
    return 1;
}

extern "C" int net_capture_at(int64_t timeNs, NetPlayerCapture* out) {
    std::lock_guard<std::mutex> lock(sLock);
    uint32_t n = sCount < (uint32_t)kRing ? sCount : (uint32_t)kRing;
    for (uint32_t i = 1; i <= n; i++) {
        const NetPlayerCapture& c = sRing[(sCount - i) % kRing];
        if (c.timeNs <= timeNs) {
            *out = c;
            return 1;
        }
    }
    return 0;
}

extern "C" const char* net_capture_anim_name(void) { return sAnimName; }
