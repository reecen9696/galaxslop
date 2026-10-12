// RemotePlayerPuppet (P2-05).  See the header; the GhostPlayer counterparts
// of each function are named in comments.
#include "remote_player_puppet.hpp"

#include <stdlib.h>
#include <string.h>

#include "Game/Animation/XanimeCore.hpp"
#include "Game/Animation/XanimePlayer.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/Player/J3DModelX.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "port/port.h"
#include "revolution/gx.h"

namespace {
// The player's default animation ("基本", basic stance) in Shift-JIS.
const char* kBasicAnim = "\x8a\xee\x96\x7b";
}  // namespace

RemotePlayerPuppet::RemotePlayerPuppet(const char* pName, bool luigi)
    : LiveActor(pName), mXanimePlayer(nullptr), mXanimePlayerUpper(nullptr), mAnimHash(0), mLuigi(luigi), mHidden(true) {
    PSMTXIdentity(mTargetRotationMtx);
}

// GhostPlayer::init, minus the race data, sensors and cameras.
void RemotePlayerPuppet::init(const JMapInfoIter&) {
    const char* model = mLuigi ? "GhostLuigi" : "GhostMario";
    initModelManagerWithAnm(model, nullptr, false);
    MR::connectToScene(this, MR::MovementType_Player, MR::CalcAnimType_Player, MR::DrawBufferType_None, MR::DrawType_Player);
    mVelocity.zero();
    // The animation players need the player's animation table, and the player
    // does not exist yet during scene initialisation: initAnimation() runs on
    // the first frame with state (applyState).

    MR::initShadowVolumeSphere(this, 50.0f);
    MR::invalidateClipping(this);
    makeActorAppeared();
}

// GhostPlayer::appear: the ghost's own material animations.
void RemotePlayerPuppet::appear() {
    LiveActor::appear();
    const char* model = mLuigi ? "GhostLuigi" : "GhostMario";
    MR::startBtk(this, model);
    MR::startBrk(this, model);
}

// GhostPlayer::initAnimation: the player's own animation table, so the
// captured hashes (MR::getHashCode of a player animation) resolve.
void RemotePlayerPuppet::initAnimation() {
    XanimeResourceTable* resource = MR::getPlayerXanimeResource();
    mXanimePlayer = new XanimePlayer(MR::getJ3DModel(this), resource);
    mXanimePlayer->duplicateSimpleGroup();
    mModelManager->mXanimePlayer = mXanimePlayer;
    mXanimePlayer->setDefaultAnimation(kBasicAnim);
    mXanimePlayer->getCore()->enableJointTransform(MR::getJ3DModelData(this));
    mXanimePlayerUpper = new XanimePlayer(MR::getJ3DModel(this), resource, mXanimePlayer);
    mXanimePlayerUpper->changeAnimation(kBasicAnim);
}

void RemotePlayerPuppet::applyState(const NetPlayerCapture& rState, bool valid) {
    if (mXanimePlayer == nullptr) {
        if (!MR::isExistMario()) {
            mHidden = true;
            return;
        }
        initAnimation();
    }
    mHidden = !valid || (rState.flags & NET_CAPTURE_HIDDEN) != 0;
    if (mHidden) {
        return;
    }
    mPosition.set(rState.pos[0], rState.pos[1], rState.pos[2]);
    mVelocity.set(rState.vel[0], rState.vel[1], rState.vel[2]);
    // Orientation straight from the sender's base matrix (columns: side, up,
    // front); on SMG's planets Euler angles would lose the gravity frame.
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            mTargetRotationMtx[r][c] = rState.baseMtx[r][c];
        }
        mTargetRotationMtx[r][3] = 0.0f;
    }
    // receiveGhostPacket, update flag 0x2000.
    if (rState.animHash != 0 && rState.animHash != mAnimHash) {
        mAnimHash = rState.animHash;
        mXanimePlayer->changeAnimationByHash(rState.animHash);
    }
}

void RemotePlayerPuppet::control() {}

// GhostPlayer::calcAndSetBaseMtx.
void RemotePlayerPuppet::calcAndSetBaseMtx() {
    if (MR::isDead(this)) {
        return;
    }
    MtxPtr baseMtx = MR::getJ3DModel(this)->getBaseTRMtx();
    MR::blendMtxRotate(getBaseMtx(), mTargetRotationMtx, 0.5f, baseMtx);
    MR::setMtxTrans(baseMtx, mPosition.x, mPosition.y, mPosition.z);
    MR::getJ3DModel(this)->setBaseScale(mScale);
}

// GhostPlayer::draw: the player models' direct draw path.
void RemotePlayerPuppet::draw() const {
    if (MR::isDead(this) || mHidden) {
        return;
    }
    J3DModelX* model = reinterpret_cast< J3DModelX* >(MR::getJ3DModel(this));
    model->viewCalc2();
    GXInvalidateVtxCache();
    model->mFlags.clear();
    model->directDraw(nullptr);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetColorUpdate(GX_TRUE);
    GXSetDstAlpha(GX_FALSE, 0);
}

// ---------------------------------------------------------------------------
// Per-scene management
// ---------------------------------------------------------------------------
// Puppets are created during scene initialisation, like every other actor:
// ModelManager::init queues resource creation on the game's main thread and
// waits for it, which deadlocks if called from the main thread's own frame
// update (seen: GameScene::update -> ... -> FunctionAsyncExecutor::waitForEnd).
namespace {
void* sScene = nullptr;            // the GameScene the puppets belong to
RemotePlayerPuppet* sLoopback = nullptr;
bool sAdoptOnNextScene = false;    // created during init, before the scene ran
}  // namespace

extern "C" int port_net_wants_puppets(void) {
    const char* loopback = getenv("PETARI_LOOPBACK");
    return loopback != nullptr && *loopback;
}

extern "C" void port_net_puppets_create_for_scene(void) {
    if (!port_net_wants_puppets() || MR::isStageFileSelect()) {
        return;  // not on the title / file-select stage
    }
    bool luigi = MR::isPlayerLuigi();
    sLoopback = new RemotePlayerPuppet("RemotePlayerPuppet", luigi);
    sLoopback->initWithoutIter();
    sAdoptOnNextScene = true;
    port_log("puppet: created for the new scene (%s)", luigi ? "Luigi" : "Mario");
}

extern "C" void port_net_puppets_update(void* pScene) {
    static const char* loopback = getenv("PETARI_LOOPBACK");
    if (!loopback || !*loopback) {
        return;
    }
    if (pScene != sScene) {
        sScene = pScene;
        if (sAdoptOnNextScene) {
            sAdoptOnNextScene = false;
        } else {
            sLoopback = nullptr;  // the old scene's actors died with its heap
        }
    }
    if (sLoopback == nullptr) {
        return;
    }
    NetPlayerCapture now;
    if (!net_capture_latest(&now)) {
        return;
    }
    int64_t delayNs = (int64_t)(atof(loopback) * 1e9);
    NetPlayerCapture past;
    bool valid = net_capture_at(now.timeNs - delayNs, &past) && past.stageHash == now.stageHash;
    sLoopback->applyState(past, valid);
}
