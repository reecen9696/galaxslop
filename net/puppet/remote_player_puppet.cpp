// RemotePlayerPuppet (P2-05).  See the header; the GhostPlayer counterparts
// of each function are named in comments.
#include "remote_player_puppet.hpp"

#include <stdlib.h>
#include <string.h>

#include "Game/Animation/XanimeCore.hpp"
#include "Game/Animation/XanimePlayer.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/Player/J3DModelX.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/ModelHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SchedulerUtil.hpp"
#include "net/client/net_client.h"
#include "net/puppet/puppet_tint.h"
#include "port/port.h"
#include "revolution/gx.h"
#include <JSystem/J3DGraphAnimator/J3DJoint.hpp>
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>
#include <JSystem/J3DGraphBase/J3DPacket.hpp>
#include <JSystem/J3DGraphBase/J3DShape.hpp>
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <revolution/gd/GDBase.h>

extern "C" {
void GDSetTexImgPtr(GXTexMapID, void*);
}

namespace {
// The player's default animation ("基本", basic stance) in Shift-JIS.
const char* kBasicAnim = "\x8a\xee\x96\x7b";

// A display list binding `pImage` to texture map 0, for J3DModelX's
// per-material DL hook (MarioActor::createTextureDL).
u8* makeTextureDL(const void* pImage, u16* pSize) {
    u8 tempDL[0x40] ATTRIBUTE_ALIGN(32);
    u32 size;
    {
        MR::ProhibitSchedulerAndInterrupts prohibit(false);  // __GDCurrentDL is global
        GDLObj obj;
        GDInitGDLObj(&obj, tempDL, sizeof(tempDL));
        __GDCurrentDL = &obj;
        GDSetTexImgPtr(GX_TEXMAP0, const_cast< void* >(pImage));
        GDPadCurr32();
        size = (u32)(obj.ptr - obj.start);
        __GDCurrentDL = nullptr;
    }
    u8* dl = new (0x20) u8[(size + 0x1F) & ~0x1F];
    memcpy(dl, tempDL, size);
    *pSize = (u16)size;
    return dl;
}
}  // namespace

ModelTint::ModelTint() : mLuigi(false), mTint(net::kTintClassic) { memset(mTextures, 0, sizeof(mTextures)); }

void ModelTint::init(J3DModelData* pData, bool luigi) {
    mLuigi = luigi;
    mTint = net::kTintClassic;
    // Shirt and sleeves, cap (texture slot 0 of each).
    initTexture(&mTextures[0], pData, "Body1_v");
    initTexture(&mTextures[1], pData, "Cap_v");
}

void ModelTint::initTexture(Texture* pTex, J3DModelData* pData, const char* pMaterialName) {
    u16 materialNo = MR::getMaterialNo(pData, pMaterialName);
    J3DMaterial* material = pData->getMaterialNodePointer(materialNo);
    ResTIMG* img = MR::getResTIMG(pData, material->getTexNo(0));
    if (img->mFormat != GX_TF_CMPR) {
        port_log("puppet: %s texture is format %u, not CMPR; no tint", pMaterialName, img->mFormat);
        return;
    }
    pTex->mMaterialNo = materialNo;
    pTex->mSource = reinterpret_cast< const u8* >(img) + img->mImageDataOffset;
    pTex->mSize = (u32)img->mWidth * img->mHeight / 2;
    pTex->mCopy = new (0x20) u8[pTex->mSize];
    memcpy(pTex->mCopy, pTex->mSource, pTex->mSize);
    pTex->mDL = makeTextureDL(pTex->mCopy, &pTex->mDLSize);
}

void ModelTint::install(J3DModelX* pModel) const {
    for (const Texture& t : mTextures) {
        if (t.mDL != nullptr) {
            pModel->_1C8[t.mMaterialNo] = t.mDL;
            pModel->_1CC[t.mMaterialNo] = t.mDLSize;
        }
    }
}

void ModelTint::set(u8 tint) {
    if (tint == mTint) {
        return;
    }
    mTint = tint;
    for (Texture& t : mTextures) {
        if (t.mCopy != nullptr) {
            net::tintCmpr(t.mSource, t.mCopy, t.mSize, mLuigi, tint);
        }
    }
}

namespace {
// The player animations whose track weights carry meaning, so the frame
// controller keeps looping them (receiveGhostPacket's isSpecialAnim list):
// 基本, 壁押し, しゃがみ歩き, がんばり走り, スケートアクセルジャンプ,
// スケートジャンプ2, スケートジャンプ3.
const char* const kLoopingAnims[] = {
    "\x8a\xee\x96\x7b",
    "\x95\xc7\x89\x9f\x82\xb5",
    "\x82\xb5\x82\xe1\x82\xaa\x82\xdd\x95\xe0\x82\xab",
    "\x82\xaa\x82\xf1\x82\xce\x82\xe8\x91\x96\x82\xe8",
    "\x83\x58\x83\x50\x81\x5b\x83\x67\x83\x41\x83\x4e\x83\x5a\x83\x8b\x83\x57\x83\x83\x83\x93\x83\x76",
    "\x83\x58\x83\x50\x81\x5b\x83\x67\x83\x57\x83\x83\x83\x93\x83\x76" "2",
    "\x83\x58\x83\x50\x81\x5b\x83\x67\x83\x57\x83\x83\x83\x93\x83\x76" "3",
};

// The player's light display list (MarioActor::updateLightDL: ambient,
// material colour and fog from where the player stands); null before the
// player exists.  Puppets are drawn in the same light as the local player.
const u8* playerLightDL(u32* pSize) {
    if (!MR::isExistMario()) {
        return nullptr;
    }
    const MarioActor* actor = MarioAccess::getPlayerActor();
    *pSize = actor->mDLSize;
    return actor->mDL[actor->mCurrDL];
}

// Shape hide flags (bit 0) of a model's data, saved and restored around a
// puppet's draw: the hand, face and body data are shared with the local
// player, which hides and shows joints on them for its own poses.
struct HideFlags {
    explicit HideFlags(J3DModelData* pData) : mData(pData) {
        mCount = pData->getShapeNum() < kMax ? pData->getShapeNum() : kMax;
        for (u16 i = 0; i < mCount; i++) {
            mHidden[i] = pData->getShapeNodePointer(i)->mFlags & 1;
        }
    }
    ~HideFlags() {
        for (u16 i = 0; i < mCount; i++) {
            J3DShape* shape = mData->getShapeNodePointer(i);
            shape->mFlags = (shape->mFlags & ~1) | mHidden[i];
        }
    }
    static const u16 kMax = 256;
    J3DModelData* mData;
    u16 mCount;
    u8 mHidden[kMax];
};
}  // namespace

RemotePlayerPuppet::RemotePlayerPuppet(const char* pName, bool luigi)
    : LiveActor(pName), mXanimePlayer(nullptr), mXanimePlayerUpper(nullptr), mAnimHash(0), mLuigi(luigi), mHidden(true), mFace(nullptr),
      mHandL(nullptr), mHandR(nullptr), mFacePose(0), mHandPoseL(0), mHandPoseR(0), mEyelidMaterial(0xFFFF), mEyelid(0) {
    PSMTXIdentity(mTargetRotationMtx);
    memset(mEyelidDL, 0, sizeof(mEyelidDL));
    memset(mEyelidDLSize, 0, sizeof(mEyelidDLSize));
}

// MarioActor::initFace / initHand for one part: its own model instance,
// whose packets draw with the body's material for that joint
// (MarioActor::copyMaterial).  `packets` 0: one per joint but the root.
ModelHolder* RemotePlayerPuppet::initPart(const char* pName, const char* pBodyJoint, u32 packets) {
    ModelHolder* part = new ModelHolder(pName, true);
    if (packets == 0) {
        packets = MR::getJointNum(part) - 1;
    }
    MR::initDLMakerFog(part, true);
    MR::newDifferedDLBuffer(part);
    J3DModelData* body = MR::getJ3DModelData(this);
    J3DModel* model = MR::getJ3DModel(part);
    J3DMaterial* material = body->getJointNodePointer(MR::getJointIndex(this, pBodyJoint))->getMesh();
    if (material == nullptr) {
        return part;
    }
    u16 materialIndex = material->mIndex;
    for (u16 i = 0; i < packets; i++) {
        J3DMatPacket* matPacket = model->getMatPacket(i);
        matPacket->mpMaterial = body->getMaterialNodePointer(materialIndex);
        J3DShapePacket* shapePacket = &model->mShapePacket[i];
        matPacket->mpInitShapePacket = shapePacket;
        matPacket->addShapePacket(shapePacket);
        matPacket->mpTexture = body->mMaterialTable.mTexture;
        matPacket->mpDisplayListObj = body->getMaterialNodePointer(materialIndex)->mSharedDLObj;
    }
    return part;
}

// GhostPlayer::init, minus the race data, sensors and cameras.
void RemotePlayerPuppet::init(const JMapInfoIter&) {
    // The player's own archive (one of them is stationed with the save's
    // character; SceneDataInitializer mounts the other with the stage).
    initModelManagerWithAnm(mLuigi ? "Luigi" : "Mario", nullptr, false);
    mTint.init(MR::getJ3DModelData(this), mLuigi);
    mTint.install(reinterpret_cast< J3DModelX* >(MR::getJ3DModel(this)));

    // MarioActor::swapTextureInit: the four eyelid frames.
    J3DModelData* data = MR::getJ3DModelData(this);
    s32 eyelid0 = data->mMaterialTable.mTextureName->getIndex("mario_eyeLid.0");
    if (eyelid0 >= 0) {
        for (u16 e = 0; e < 4; e++) {
            const ResTIMG* img = MR::getResTIMG(data, eyelid0 + e);
            mEyelidDL[e] = makeTextureDL(reinterpret_cast< const u8* >(img) + img->mImageDataOffset, &mEyelidDLSize[e]);
        }
        mEyelidMaterial = MR::getMaterialNo(data, "EyeLid_v");
    }

    // MarioActor::initFace: the face shares the body's texture table.
    mFace = initPart(mLuigi ? "LuigiFace" : "MarioFace", "Face0", 8);
    MR::getJ3DModelData(mFace)->mMaterialTable.mTexture = MR::getJ3DModelData(this)->mMaterialTable.mTexture;
    MR::getJ3DModelData(mFace)->mMaterialTable.mTextureName = MR::getJ3DModelData(this)->mMaterialTable.mTextureName;
    // MarioActor::initHand (the hand models are shared by Mario and Luigi):
    // one packet per pose joint.
    mHandL = initPart("MarioHandL", "HandL0", 0);
    mHandR = initPart("MarioHandR", "HandR0", 0);
    MR::connectToScene(this, MR::MovementType_Player, MR::CalcAnimType_Player, MR::DrawBufferType_None, MR::DrawType_Player);
    mVelocity.zero();
    // The animation players need the player's animation table, and the player
    // does not exist yet during scene initialisation: initAnimation() runs on
    // the first frame with state (applyState).

    MR::initShadowVolumeSphere(this, 50.0f);
    MR::invalidateClipping(this);
    makeActorAppeared();
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
        const char* name = mXanimePlayer->getCurrentAnimationName();
        bool looping = false;
        for (const char* l : kLoopingAnims) {
            looping |= name != nullptr && strcmp(name, l) == 0;
        }
        if (!looping) {
            mXanimePlayer->_20->mAttribute = 1;
        }
    }
    // receiveGhostPacket, flag 0x0020: the sender's frame, so the puppet is
    // at the same point of the same animation instead of on its own clock.
    mXanimePlayer->getFrameCtrl()->setFrame(rState.animFrame);
    mEyelid = (u8)((rState.flags & NET_CAPTURE_EYELID_MASK) >> NET_CAPTURE_EYELID_SHIFT);
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
        port_log("puppet: weights %.2f %.2f %.2f %.2f; eyelid %u", rState.animWeights[0], rState.animWeights[1], rState.animWeights[2],
                 rState.animWeights[3], mEyelid);
    }
}

void RemotePlayerPuppet::control() {}

// MarioActor::calcViewAndEntry's part: face and hands follow the body's
// joints; MarioActor::updateFace / updateHand: the expression and hand
// poses are encoded in the animation's PartsControl joint (x face, y left
// hand, z right hand, 10 units per pose).  Called from draw(): puppets have
// no draw buffer, so the scene never calls their calcViewAndEntry.
void RemotePlayerPuppet::updateParts() {
    if (mXanimePlayer == nullptr || mFace == nullptr) {
        return;
    }
    mHandL->updateMtx(MR::getJointMtx(this, "HandL"));
    mHandR->updateMtx(MR::getJointMtx(this, "HandR"));
    mFace->updateMtx(MR::getJointMtx(this, "Face0"));
    TVec3f control(0.0f, 0.0f, 0.0f);
    mXanimePlayer->getMainAnimationTrans(MR::getJointIndex(this, "PartsControl"), &control);
    mFacePose = (u8)MR::clamp((s32)(0.49f + control.x / 10.0f), 0, 7);
    mHandPoseL = (u8)MR::clamp((s32)(0.49f + control.y / 10.0f), 0, 7);
    mHandPoseR = (u8)MR::clamp((s32)(0.49f + control.z / 10.0f), 0, 7);
    static const bool sLog = getenv("PETARI_PUPPETLOG") != nullptr;
    static u32 sN = 0;
    if (sLog && (sN++ % 60) == 0) {
        MtxPtr f = MR::getJ3DModel(mFace)->mBaseTransformMtx;
        port_log("puppet parts: pos %.0f %.0f %.0f face at %.0f %.0f %.0f; poses face %u hands %u %u; face shapes %u", mPosition.x, mPosition.y,
                 mPosition.z, f[0][3], f[1][3], f[2][3], mFacePose, mHandPoseL, mHandPoseR, MR::getJ3DModelData(mFace)->getShapeNum());
    }
}

// A face or hand model with only the joint of `pose` shown (joint 0 is the
// root), in the player's light.
void RemotePlayerPuppet::drawPart(ModelHolder* pPart, u8 pose, u32 poses) const {
    HideFlags keep(MR::getJ3DModelData(pPart));
    for (u32 i = 0; i < poses; i++) {
        J3DJoint* joint = MR::getJoint(pPart, (u16)(i + 1));
        if (i == pose) {
            MR::showJoint(joint);
        } else {
            MR::hideJoint(joint);
        }
    }
    J3DModelX* model = reinterpret_cast< J3DModelX* >(MR::getJ3DModel(pPart));
    u32 lightSize = 0;
    const u8* light = playerLightDL(&lightSize);
    model->setDynamicDL(const_cast< u8* >(light), lightSize);
    model->directDraw(nullptr);
}

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

// MarioActor::drawMarioModel + drawHand + the face, for the normal suit.
void RemotePlayerPuppet::draw() const {
    if (MR::isDead(this) || mHidden) {
        return;
    }
    u32 lightSize = 0;
    const u8* light = playerLightDL(&lightSize);
    J3DModelX* model = reinterpret_cast< J3DModelX* >(MR::getJ3DModel(this));
    {
        // The body's own face and hands stay hidden (the parts replace them);
        // the local player hides them on its data, but not on the other
        // character's.
        HideFlags keep(MR::getJ3DModelData(this));
        MR::hideJoint(model, "Face0");
        MR::hideJoint(model, "HandL0");
        MR::hideJoint(model, "HandR0");
        model->mFlags.clear();
        if (mEyelidMaterial != 0xFFFF && mEyelidDL[mEyelid & 3] != nullptr) {
            model->_1C8[mEyelidMaterial] = mEyelidDL[mEyelid & 3];  // MarioActor::swapTexture
            model->_1CC[mEyelidMaterial] = mEyelidDLSize[mEyelid & 3];
        }
        model->setDynamicDL(const_cast< u8* >(light), lightSize);
        model->viewCalc2();  // GhostPlayer::draw (setDrawView needs the player's extra buffers)
        GXInvalidateVtxCache();
        model->directDraw(nullptr);
        model->mFlags.clear();
    }
    if (mFace != nullptr && mXanimePlayer != nullptr) {
        const_cast< RemotePlayerPuppet* >(this)->updateParts();
        drawPart(mHandL, mHandPoseL, MR::getJointNum(mHandL) - 1);
        drawPart(mHandR, mHandPoseR, MR::getJointNum(mHandR) - 1);
        drawPart(mFace, mFacePose, 8);
    }
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
// Per remote slot (net::netRemote) a Mario and a Luigi puppet, created
// during scene init (a remote's character is not known then, and actors
// cannot be created later) and hidden until that slot has a player here;
// only the one matching the remote's character is shown.  The loopback test
// uses slot 0.
RemotePlayerPuppet* sPuppets[net::kMaxRemotes][2] = {};
bool sAdoptOnNextScene = false;  // created during init, before the scene ran
// The local player's own colour (players are the same colour on every
// screen): textures prepared with the scene's puppets, hooked into the
// player's model once it exists.
ModelTint* sLocalTint = nullptr;
J3DModelX* sLocalTintModel = nullptr;  // the player model it is installed on

const char* loopbackDelay() {
    const char* v = getenv("PETARI_LOOPBACK");
    return v && *v ? v : nullptr;
}

void forgetPuppets() {
    for (auto& slot : sPuppets) {
        slot[0] = slot[1] = nullptr;
    }
    sLocalTint = nullptr;
    sLocalTintModel = nullptr;
}

void updateLocalTint() {
    if (sLocalTint == nullptr || !MR::isExistMario()) {
        return;
    }
    J3DModelX* model = reinterpret_cast< J3DModelX* >(MR::getJ3DModel(MarioAccess::getPlayerActor()));
    if (model != sLocalTintModel) {
        sLocalTintModel = model;
        sLocalTint->install(model);
    }
    const char* tint = getenv("SMG_TINT");
    sLocalTint->set(net::puppetTint(tint ? (u8)atoi(tint) : 0, net::netLocalId()));
}

// Shows the slot's puppet for `luigi` posed by `pose`, hides the other.
void showSlot(int slot, bool luigi, const NetPlayerCapture& pose, bool visible, u8 tint) {
    for (int c = 0; c < 2; c++) {
        RemotePlayerPuppet* p = sPuppets[slot][c];
        if (p == nullptr) {
            continue;
        }
        bool mine = (c == 1) == luigi;
        if (mine) {
            p->setTint(tint);
        }
        p->applyState(pose, mine && visible);
    }
}
}  // namespace

extern "C" int port_net_wants_puppets(void) { return loopbackDelay() != nullptr || net::netEnabled(); }

extern "C" void port_net_puppets_create_for_scene(void) {
    if (!port_net_wants_puppets() || MR::isStageFileSelect()) {
        return;  // not on the title / file-select stage
    }
    int count = net::netEnabled() ? net::kMaxRemotes : 1;
    forgetPuppets();
    for (int i = 0; i < count; i++) {
        for (int c = 0; c < 2; c++) {
            sPuppets[i][c] = new RemotePlayerPuppet(c ? "RemoteLuigiPuppet" : "RemoteMarioPuppet", c == 1);
            sPuppets[i][c]->initWithoutIter();
        }
    }
    if (net::netEnabled()) {
        // Same model data as the player's (the puppet of its character).
        sLocalTint = new ModelTint();
        sLocalTint->init(MR::getJ3DModelData(sPuppets[0][MR::isPlayerLuigi() ? 1 : 0]), MR::isPlayerLuigi());
    }
    sAdoptOnNextScene = true;
    port_log("puppet: %d Mario + %d Luigi created for the new scene", count, count);
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
            forgetPuppets();  // the old scene's actors died with its heap
        }
    }
    NetPlayerCapture now;
    bool haveLocal = net_capture_latest(&now) != 0;

    if (net::netEnabled()) {
        net::netTick(haveLocal ? &now : nullptr);
        updateLocalTint();
        for (int i = 0; i < net::kMaxRemotes; i++) {
            const net::RemoteView& v = net::netRemote(i);
            showSlot(i, v.luigi, v.pose, v.visible, net::puppetTint(v.tint, v.id));
        }
        return;
    }
    if (const char* delay = loopbackDelay()) {
        if (!haveLocal) {
            return;
        }
        int64_t delayNs = (int64_t)(atof(delay) * 1e9);
        NetPlayerCapture past;
        bool valid = net_capture_at(now.timeNs - delayNs, &past) && past.stageHash == now.stageHash;
        // SMG_TINT picks the copy's colour, otherwise as player 2 (green).
        const char* tint = getenv("SMG_TINT");
        showSlot(0, (past.flags & NET_CAPTURE_LUIGI) != 0, past, valid, net::puppetTint(tint ? (u8)atoi(tint) : 0, 2));
    }
}
