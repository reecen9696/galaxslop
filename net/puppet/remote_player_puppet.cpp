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
#include "net/client/net_client.h"
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
    // GhostPlayer::setAnimationWeight: the locomotion group ("basic") blends
    // its tracks by speed; without the weights the puppet stands still while
    // it slides along.
    for (int i = 0; i < 4; i++) {
        mXanimePlayer->changeTrackWeight(i, rState.animWeights[i]);
    }
    // PETARI_PUPPETLOG=1: what the puppet was given and did, once a second.
    static const bool sLog = getenv("PETARI_PUPPETLOG") != nullptr;
    static u32 sN = 0;
    if (sLog && (sN++ % 60) == 0) {
        port_log("puppet: hash %08x -> playing '%s'; side %.2f %.2f %.2f up %.2f %.2f %.2f front %.2f %.2f %.2f", rState.animHash,
                 mXanimePlayer->getCurrentAnimationName(), rState.baseMtx[0][0], rState.baseMtx[1][0], rState.baseMtx[2][0], rState.baseMtx[0][1],
                 rState.baseMtx[1][1], rState.baseMtx[2][1], rState.baseMtx[0][2], rState.baseMtx[1][2], rState.baseMtx[2][2]);
        port_log("puppet: weights %.2f %.2f %.2f %.2f", rState.animWeights[0], rState.animWeights[1], rState.animWeights[2], rState.animWeights[3]);
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
void* sScene = nullptr;  // the GameScene the puppets belong to
// One puppet per remote slot (net::netRemote), created during scene init and
// hidden until that slot has a player here; the loopback test uses slot 0.
RemotePlayerPuppet* sPuppets[net::kMaxRemotes] = {};
bool sAdoptOnNextScene = false;  // created during init, before the scene ran

const char* loopbackDelay() {
    const char* v = getenv("PETARI_LOOPBACK");
    return v && *v ? v : nullptr;
}
}  // namespace

extern "C" int port_net_wants_puppets(void) { return loopbackDelay() != nullptr || net::netEnabled(); }

extern "C" void port_net_puppets_create_for_scene(void) {
    if (!port_net_wants_puppets() || MR::isStageFileSelect()) {
        return;  // not on the title / file-select stage
    }
    int count = net::netEnabled() ? net::kMaxRemotes : 1;
    for (int i = 0; i < net::kMaxRemotes; i++) {
        sPuppets[i] = nullptr;
    }
    for (int i = 0; i < count; i++) {
        sPuppets[i] = new RemotePlayerPuppet("RemotePlayerPuppet", false);
        sPuppets[i]->initWithoutIter();
    }
    sAdoptOnNextScene = true;
    port_log("puppet: %d created for the new scene", count);
}

extern "C" void port_net_puppets_update(void* pScene) {
    if (!port_net_wants_puppets()) {
        return;
    }
    if (pScene != sScene) {
        sScene = pScene;
        if (sAdoptOnNextScene) {
            sAdoptOnNextScene = false;
        } else {
            for (RemotePlayerPuppet*& p : sPuppets) p = nullptr;  // the old scene's actors died with its heap
        }
    }
    NetPlayerCapture now;
    bool haveLocal = net_capture_latest(&now) != 0;

    if (net::netEnabled()) {
        net::netTick(haveLocal ? &now : nullptr);
        for (int i = 0; i < net::kMaxRemotes; i++) {
            if (sPuppets[i] != nullptr) {
                const net::RemoteView& v = net::netRemote(i);
                sPuppets[i]->applyState(v.pose, v.visible);
            }
        }
        return;
    }
    if (const char* delay = loopbackDelay()) {
        if (sPuppets[0] == nullptr || !haveLocal) {
            return;
        }
        int64_t delayNs = (int64_t)(atof(delay) * 1e9);
        NetPlayerCapture past;
        bool valid = net_capture_at(now.timeNs - delayNs, &past) && past.stageHash == now.stageHash;
        sPuppets[0]->applyState(past, valid);
    }
}
