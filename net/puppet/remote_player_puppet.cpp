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
#include "Game/Util/DemoUtil.hpp"
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

    // GhostPlayer::initAnimation: the player's own animation table, so the
    // captured hashes (MR::getHashCode of a player animation) resolve.
    XanimeResourceTable* resource = MR::getPlayerXanimeResource();
    mXanimePlayer = new XanimePlayer(MR::getJ3DModel(this), resource);
    mXanimePlayer->duplicateSimpleGroup();
    mModelManager->mXanimePlayer = mXanimePlayer;
    mXanimePlayer->setDefaultAnimation(kBasicAnim);
    mXanimePlayer->getCore()->enableJointTransform(MR::getJ3DModelData(this));
    mXanimePlayerUpper = new XanimePlayer(MR::getJ3DModel(this), resource, mXanimePlayer);
    mXanimePlayerUpper->changeAnimation(kBasicAnim);

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

void RemotePlayerPuppet::applyState(const NetPlayerCapture& rState, bool valid) {
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
namespace {
void* sScene = nullptr;  // the GameScene the puppets belong to
RemotePlayerPuppet* sLoopback = nullptr;
}  // namespace

extern "C" void port_net_puppets_update(void* pScene) {
    static const char* loopback = getenv("PETARI_LOOPBACK");
    if (!loopback || !*loopback) {
        return;
    }
    static int sQuietFrames = 0;
    if (pScene != sScene) {
        // A new scene: the old one's actors died with its heap.
        sScene = pScene;
        sLoopback = nullptr;
        sQuietFrames = 0;
    }
    // Create puppets only after 2 s of ordinary gameplay (no cutscene), well
    // clear of the scene's own loading, as PETARI_STAGE's jump does.
    if (MR::isDemoActive()) {
        sQuietFrames = 0;
    } else if (sQuietFrames < 120) {
        sQuietFrames++;
    }
    NetPlayerCapture now;
    if (!net_capture_latest(&now)) {
        return;
    }
    if (sLoopback == nullptr && sQuietFrames >= 120) {
        sLoopback = new RemotePlayerPuppet("RemotePlayerPuppet", (now.flags & NET_CAPTURE_LUIGI) != 0);
        sLoopback->initWithoutIter();
        port_log("puppet: loopback puppet created (%.1f s behind)", atof(loopback));
    }
    if (sLoopback == nullptr) {
        return;
    }
    int64_t delayNs = (int64_t)(atof(loopback) * 1e9);
    NetPlayerCapture past;
    bool valid = net_capture_at(now.timeNs - delayNs, &past) && past.stageHash == now.stageHash;
    sLoopback->applyState(past, valid);
}
