// RemotePlayerPuppet (P2-05).  See the header; the GhostPlayer counterparts
// of each function are named in comments.
#include "remote_player_puppet.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>

#include "Game/Animation/XanimeCore.hpp"
#include "Game/Animation/XanimePlayer.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/Map/LightFunction.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/LiveActor/ActorLightCtrl.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Player/DrawAdaptor.hpp"
#include "Game/Player/J3DModelX.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/ModelHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/Functor.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SchedulerUtil.hpp"
#include "net/client/net_client.h"
#include "net/puppet/puppet_tint.h"
#include "net/world/world_sync.h"
#include "port/port.h"
#include "revolution/gx.h"
#include <JSystem/J3DGraphAnimator/J3DJoint.hpp>
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>
#include <JSystem/J3DGraphBase/J3DPacket.hpp>
#include <JSystem/J3DGraphBase/J3DShape.hpp>
#include <JSystem/J3DGraphBase/J3DSys.hpp>
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/JKernel/JKRHeap.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <revolution/gd/GDBase.h>
#include <revolution/gd/GDGeometry.h>
#include <revolution/gd/GDLight.h>

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
        GDLObj* previous = __GDCurrentDL;
        GDLObj obj;
        GDInitGDLObj(&obj, tempDL, sizeof(tempDL));
        __GDCurrentDL = &obj;
        GDSetTexImgPtr(GX_TEXMAP0, const_cast< void* >(pImage));
        GDPadCurr32();
        size = (u32)(obj.ptr - obj.start);
        __GDCurrentDL = previous;
    }
    u8* dl = new (0x20) u8[(size + 0x1F) & ~0x1F];
    memcpy(dl, tempDL, size);
    *pSize = (u16)size;
    return dl;
}
}  // namespace

ModelTint::ModelTint() : mLuigi(false), mTint(net::kTintClassic), mSuit(0) { memset(mTextures, 0, sizeof(mTextures)); }

void ModelTint::init(J3DModelData* pData, bool luigi) {
    mLuigi = luigi;
    mTint = net::kTintClassic;
    mSuit = 0;
    // Shirt and sleeves, cap (texture slot 0 of each).
    initTexture(&mTextures[0], pData, "Body1_v", "MarioBody");
    initTexture(&mTextures[1], pData, "Cap_v", "MarioCap");
}

void ModelTint::initTexture(Texture* pTex, J3DModelData* pData, const char* pMaterialName, const char* pTexturePrefix) {
    u16 materialNo = MR::getMaterialNo(pData, pMaterialName);
    J3DMaterial* material = pData->getMaterialNodePointer(materialNo);
    ResTIMG* img = MR::getResTIMG(pData, material->getTexNo(0));
    if (img->mFormat != GX_TF_CMPR) {
        port_log("puppet: %s texture is format %u, not CMPR; no tint", pMaterialName, img->mFormat);
        return;
    }
    u32 size = (u32)img->mWidth * img->mHeight / 2;
    for (int suit = 0; suit < 3; suit++) {
        // "<prefix>.<suit>", same size and format; fall back to the bound one.
        char name[32];
        snprintf(name, sizeof(name), "%s.%d", pTexturePrefix, suit);
        s32 index = pData->mMaterialTable.mTextureName->getIndex(name);
        const ResTIMG* variant = index >= 0 ? MR::getResTIMG(pData, index) : img;
        if (variant->mFormat != img->mFormat || variant->mWidth != img->mWidth || variant->mHeight != img->mHeight) {
            variant = img;
        }
        pTex->mSource[suit] = reinterpret_cast< const u8* >(variant) + variant->mImageDataOffset;
    }
    pTex->mMaterialNo = materialNo;
    pTex->mSize = size;
    pTex->mCopy = new (0x20) u8[size];
    memcpy(pTex->mCopy, pTex->mSource[0], size);
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

void ModelTint::set(u8 tint, u8 suit) {
    suit = suit < 3 ? suit : 0;
    if (tint == mTint && suit == mSuit) {
        return;
    }
    mTint = tint;
    mSuit = suit;
    for (Texture& t : mTextures) {
        if (t.mCopy != nullptr) {
            net::tintCmpr(t.mSource[suit], t.mCopy, t.mSize, mLuigi, tint);
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
      mHandL(nullptr), mHandR(nullptr), mBeeWing(nullptr), mTeresa(nullptr), mMode(0), mRemoteId(0), mSolid(false), mTrampleCooldown(0),
      mAirborne(false), mLightDLSize(0), mLightDLIndex(0), mPendingLight(nullptr), mFacePose(0), mHandPoseL(0),
      mHandPoseR(0), mEyelidMaterial(0xFFFF), mEyelid(0) {
    PSMTXIdentity(mTargetRotationMtx);
    mLightDL[0] = mLightDL[1] = nullptr;
    for (Form& f : mForms) {
        f.mBody = f.mHandL = f.mHandR = nullptr;
        f.mTinted = false;
    }
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
    initForms();
    MR::connectToScene(this, MR::MovementType_Player, MR::CalcAnimType_Player, MR::DrawBufferType_None, MR::DrawType_Player);
    mVelocity.zero();
    // The animation players need the player's animation table, and the player
    // does not exist yet during scene initialisation: initAnimation() runs on
    // the first frame with state (applyState).

    // Lit where it stands (lightDL), not where the local player does.
    // The light controller is allocated now and attached on the first shown
    // frame (applyState): the area lights do not exist during scene init.
    initActorLightCtrl();
    mPendingLight = mActorLightCtrl;
    mActorLightCtrl = nullptr;
    mLightDL[0] = new (0x20) u8[0x80];
    mLightDL[1] = new (0x20) u8[0x80];

    // GhostPlayer::init's sensor.
    initHitSensor(1);
    MR::addHitSensorRide(this, "body", 4, 40.0f, TVec3f(0.0f, 80.0f, 0.0f));
    MR::invalidateHitSensors(this);

    MR::initShadowVolumeSphere(this, 50.0f);
    MR::invalidateClipping(this);
    makeActorAppeared();
}

// GhostPlayer::attackSensor, racing branch: push the local Mario out, unless
// he is the faster of the two.  Each client moves only its own Mario, so
// with plain mutual repulsion the walker is deflected around the stander
// and the stander, seeing the walker late, barely moves.  Instead the faster
// one is the pusher: he walks on (into the puppet), and on the other
// player's client the same contact shoves the slower one out of the way.
// Equal speeds (both standing) push both apart.
void RemotePlayerPuppet::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!mSolid || !MR::isSensorPlayer(pReceiver)) {
        return;
    }
    const f32 kPusherMargin = 1.0f;  // units/frame faster than the other
    f32 localSpeed = MarioAccess::getPlayerActor()->mVelocity.length();
    if (localSpeed > mVelocity.length() + kPusherMargin) {
        return;
    }
    if (MR::sendMsgPush(pReceiver, pSender)) {
        static const bool sLog = getenv("PETARI_PUPPETLOG") != nullptr;
        static u32 sN = 0;
        if (sLog && (sN++ % 30) == 0) {
            port_log("puppet: pushing the local player (player %u)", mRemoteId);
        }
    }
}

// GhostPlayer::receiveMsgPlayerAttack without the star bits: the stomping
// Mario bounces (returning true), and the stomped player is told.
bool RemotePlayerPuppet::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (!mSolid) {
        return false;
    }
    // A jump onto the head: the stomper bounces, the other flinches.  A
    // ground pound: the other is squashed flat and loses health.
    bool hipDrop = MR::isMsgPlayerHipDrop(msg);
    if (MR::isMsgPlayerTrample(msg) || hipDrop) {
        if (mTrampleCooldown == 0 && mRemoteId != 0 && MR::isExistMario()) {
            const TVec3f& front = MarioAccess::getPlayerActor()->mMario->mFrontVec;
            float dir[3] = {front.x, front.y, front.z};
            net::netSendContact(mRemoteId, hipDrop ? net::kContactHipDrop : net::kContactTrample, dir);
        }
        if (mTrampleCooldown == 0) {
            port_log("puppet: local player %s player %u", hipDrop ? "ground-pounded" : "stomped", mRemoteId);
        }
        mTrampleCooldown = 20;
        return true;
    }
    return false;
}

// GhostPlayer swallows everything else; a puppet must not: Mario walking
// into it sends ACTMES_KICK (MarioActor::sendBodyAttack) and, when that is
// accepted, plays the running kick instead of walking, every frame.
bool RemotePlayerPuppet::receiveOtherMsg(u32, HitSensor*, HitSensor*) {
    return false;
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

void RemotePlayerPuppet::applyState(const NetPlayerCapture& rState, bool valid, bool solid, u8 remoteId) {
    mRemoteId = remoteId;
    solid = solid && valid && (rState.flags & NET_CAPTURE_HIDDEN) == 0 && mXanimePlayer != nullptr;
    if (solid != mSolid) {
        mSolid = solid;
        if (solid) {
            MR::validateHitSensors(this);
        } else {
            MR::invalidateHitSensors(this);
        }
    }
    if (mXanimePlayer == nullptr) {
        if (!MR::isExistMario()) {
            mHidden = true;
            return;
        }
        initAnimation();
    }
    mHidden = !valid || (rState.flags & NET_CAPTURE_HIDDEN) != 0;
    if (mHidden) {
        showParts(false, false);
        return;
    }
    mPosition.set(rState.pos[0], rState.pos[1], rState.pos[2]);
    if (mPendingLight != nullptr) {
        // MR::initLightCtrl looks the light type up in the actor's draw
        // buffer, which a puppet has none of.  init(type) takes it directly
        // (and skips the draw buffer's light sort): the local player's type.
        mActorLightCtrl = mPendingLight;
        mPendingLight = nullptr;
        mActorLightCtrl->init(MarioAccess::getPlayerActor()->mActorLightCtrl->_4, false);
    }
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
    mMode = (u8)((rState.flags & NET_CAPTURE_MODE_MASK) >> NET_CAPTURE_MODE_SHIFT);
    // Flattened like the sender (MarioCrush scales y to 0.2).
    if (rState.flags & NET_CAPTURE_SQUASHED) {
        mScale.set(1.0f, 0.2f, 1.0f);
    } else {
        mScale.set(1.0f, 1.0f, 1.0f);
    }
    bool airborne = (rState.flags & NET_CAPTURE_GROUNDED) == 0;
    // Form animations advance like MarioActor's hidden holders do: movement()
    // on a killed actor still updates its frame controllers.
    int form = formForMode(mMode);
    if (form >= 0 && mForms[form].mBody != nullptr) {
        // MarioActor::calcAnim: the active form's material DLs
        // (MR::updateModelDiffDL on _9E4, _9C0 and its hands, _9C8 ...).
        MR::updateModelDiffDL(mForms[form].mBody);
        if (form == kFormIce && mForms[form].mHandL != nullptr && mForms[form].mHandR != nullptr) {
            MR::updateModelDiffDL(mForms[form].mHandL);
            MR::updateModelDiffDL(mForms[form].mHandR);
        }
    }
    if (form == kFormRainbow && mForms[form].mBody != nullptr) {
        Form& f = mForms[kFormRainbow];
        f.mBody->movement();
        if (f.mHandL != nullptr && f.mHandR != nullptr) {
            f.mHandL->movement();
            f.mHandR->movement();
            MR::setBrkFrame(f.mHandL, MR::getBrkFrame(f.mBody));  // calcAnimInMovement
            MR::setBrkFrame(f.mHandR, MR::getBrkFrame(f.mBody));
            MR::updateModelDiffDL(f.mHandL);
            MR::updateModelDiffDL(f.mHandR);
        }
    }
    showParts(form == kFormBee, mMode == 6);
    if (form == kFormBee && mBeeWing != nullptr) {
        // MarioActor::updateBeeWingAnimation, by grounded state only.
        if (airborne != mAirborne || !MR::isBckPlaying(mBeeWing, airborne ? "Fly" : "Wait")) {
            MR::startBck(mBeeWing, airborne ? "Fly" : "Wait");
        }
    }
    if (mMode == 6 && mTeresa != nullptr) {
        // The boo has its own animations; airborne or moving: fly, else wait.
        const char* anim = airborne || mVelocity.length() > 2.0f ? "fly" : "wait";
        if (!MR::isBckPlaying(mTeresa, anim)) {
            MR::startBck(mTeresa, anim);
        }
    }
    mAirborne = airborne;
    updateLightDL();
    // GhostPlayer::setAnimationWeight: the locomotion group ("basic") blends
    // its tracks by speed; without the weights the puppet stands still while
    // it slides along.
    for (int i = 0; i < 4; i++) {
        mXanimePlayer->changeTrackWeight(i, rState.animWeights[i]);
    }
    // PETARI_PUPPETLOG=<frames>: what the puppet was given and did, every
    // that many frames (1 = 60).
    static const int sLog = getenv("PETARI_PUPPETLOG") != nullptr ? atoi(getenv("PETARI_PUPPETLOG")) : 0;
    static u32 sN = 0;
    if (sLog > 0 && (sN++ % (u32)(sLog == 1 ? 60 : sLog)) == 0) {
        port_log("puppet: hash %08x -> playing '%s'; side %.2f %.2f %.2f up %.2f %.2f %.2f front %.2f %.2f %.2f", rState.animHash,
                 mXanimePlayer->getCurrentAnimationName(), rState.baseMtx[0][0], rState.baseMtx[1][0], rState.baseMtx[2][0], rState.baseMtx[0][1],
                 rState.baseMtx[1][1], rState.baseMtx[2][1], rState.baseMtx[0][2], rState.baseMtx[1][2], rState.baseMtx[2][2]);
        port_log("puppet: weights %.2f %.2f %.2f %.2f; eyelid %u", rState.animWeights[0], rState.animWeights[1], rState.animWeights[2],
                 rState.animWeights[3], mEyelid);
    }
}

void RemotePlayerPuppet::control() {
    if (mTrampleCooldown != 0) {
        mTrampleCooldown--;
    }
}

int RemotePlayerPuppet::formForMode(u8 mode) {
    switch (mode) {
    case 1:
        return kFormRainbow;
    case 3:
        return kFormIce;
    case 4:
        return kFormBee;
    case 5:
        return kFormSpring;
    default:
        return -1;
    }
}

// MarioActor::initBeeMario & co: a form model, killed (drawn by us, never by
// the scene).  `shareJoints`: posed by the body's animated joints, which
// works because every human form has the body's skeleton.
ModelHolder* RemotePlayerPuppet::initFormModel(const char* pName, bool shareJoints) {
    if (!MR::isLoadedObjectOrLayoutArchive(pName)) {
        return nullptr;
    }
    ModelHolder* holder = new ModelHolder(pName, true);
    holder->initWithoutIter();
    if (shareJoints) {
        reinterpret_cast< J3DModelX* >(MR::getJ3DModel(holder))->copyAnmMtxBuffer(reinterpret_cast< J3DModelX* >(MR::getJ3DModel(this)));
    }
    MR::initDLMakerFog(holder, true);
    MR::newDifferedDLBuffer(holder);
    holder->kill();
    return holder;
}

void RemotePlayerPuppet::initForms() {
    const bool l = mLuigi;
    Form& rainbow = mForms[kFormRainbow];
    rainbow.mBody = initFormModel(l ? "InvincibleLuigi" : "InvincibleMario", true);
    if (rainbow.mBody != nullptr) {
        rainbow.mHandL = initFormModel("InvincibleMarioHandL", false);
        rainbow.mHandR = initFormModel("InvincibleMarioHandR", false);
        // The colour cycle (MarioActor::setPlayerMode, mode 1).
        MR::startBrk(rainbow.mBody, "InvincibleMario");
        if (rainbow.mHandL != nullptr && rainbow.mHandR != nullptr) {
            MR::startBrk(rainbow.mHandL, "InvincibleMarioHandL");
            MR::startBrk(rainbow.mHandR, "InvincibleMarioHandR");
        }
    }
    Form& ice = mForms[kFormIce];
    ice.mBody = initFormModel(l ? "IceLuigi" : "IceMario", true);
    if (ice.mBody != nullptr) {
        ice.mHandL = initFormModel("IceMarioHandL", false);
        ice.mHandR = initFormModel("IceMarioHandR", false);
        new DrawAdaptor(MR::Functor(this, &RemotePlayerPuppet::drawIndirect), MR::DrawType_0x24);
    }
    mForms[kFormBee].mBody = initFormModel(l ? "BeeLuigi" : "BeeMario", true);
    if (mForms[kFormBee].mBody != nullptr && MR::isLoadedObjectOrLayoutArchive("BeeMarioWing")) {
        mBeeWing = new PartsModel(this, "RemoteBeeWing", "BeeMarioWing", MR::getJointMtx(this, "Spine2"), MR::DrawBufferType_PlayerDecoration,
                                  false);
        mBeeWing->initWithoutIter();
        MR::startBck(mBeeWing, "Wait");
        mBeeWing->makeActorDead();
    }
    mForms[kFormSpring].mBody = initFormModel(l ? "HopperLuigi" : "HopperMario", true);
    // Bee and spring Mario still wear the shirt and cap; tint them too.
    for (int f : {kFormBee, kFormSpring}) {
        if (mForms[f].mBody != nullptr) {
            mForms[f].mTint.init(MR::getJ3DModelData(mForms[f].mBody), mLuigi);
            mForms[f].mTint.install(reinterpret_cast< J3DModelX* >(MR::getJ3DModel(mForms[f].mBody)));
            mForms[f].mTinted = true;
        }
    }
    const char* teresa = l ? "TeresaLuigi" : "TeresaMario";
    if (MR::isLoadedObjectOrLayoutArchive(teresa)) {
        mTeresa = new PartsModel(this, "RemoteTeresa", teresa, MR::getJ3DModel(this)->getBaseTRMtx(), MR::DrawBufferType_UNK_0x17, false);
        mTeresa->initWithoutIter();
        MR::startBck(mTeresa, "wait");
        mTeresa->makeActorDead();
    }
}

void RemotePlayerPuppet::showParts(bool wing, bool teresa) {
    PartsModel* parts[] = {mBeeWing, mTeresa};
    bool show[] = {wing, teresa};
    for (int i = 0; i < 2; i++) {
        if (parts[i] == nullptr) {
            continue;
        }
        if (show[i] && MR::isDead(parts[i])) {
            parts[i]->makeActorAppeared();
        } else if (!show[i] && !MR::isDead(parts[i])) {
            parts[i]->makeActorDead();
        }
    }
}

void RemotePlayerPuppet::setTint(u8 tint, u8 suit) {
    mTint.set(tint, suit);
    for (Form& f : mForms) {
        if (f.mTinted) {
            f.mTint.set(tint, 0);
        }
    }
}

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
void RemotePlayerPuppet::drawPart(ModelHolder* pPart, u8 pose, u32 poses, bool lit) const {
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
    const u8* light = lit ? lightDL(&lightSize) : nullptr;
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

// One body model (the puppet's own or a form's) with the body's face and
// hand joints hidden (the separate parts replace them) unless the form
// shows its own face (ice, rainbow: MarioActor::updateFace hides the part).
void RemotePlayerPuppet::drawBody(J3DModelX* pModel, bool ownFace, bool lit) const {
    u32 lightSize = 0;
    const u8* light = lit ? lightDL(&lightSize) : nullptr;
    HideFlags keep(pModel->getModelData());
    if (ownFace) {
        MR::showJoint(pModel, "Face0");
    } else {
        MR::hideJoint(pModel, "Face0");
    }
    MR::hideJoint(pModel, "HandL0");
    MR::hideJoint(pModel, "HandR0");
    pModel->mFlags.clear();
    pModel->setDynamicDL(const_cast< u8* >(light), lightSize);
    pModel->viewCalc2();  // GhostPlayer::draw (setDrawView needs the player's extra buffers)
    GXInvalidateVtxCache();
    pModel->directDraw(nullptr);
    pModel->mFlags.clear();
}

// MarioActor::calcFogLighting + updateLightDL for the puppet: built in the
// movement phase like the player's (GD display lists are written through
// the global __GDCurrentDL, which the player's material updates also use;
// building one while drawing corrupted those, intermittently).
void RemotePlayerPuppet::updateLightDL() {
    if (mActorLightCtrl == nullptr || mActorLightCtrl->mAreaLightInf == nullptr || mLightDL[0] == nullptr) {
        return;
    }
    u8 buffer[0x80] ATTRIBUTE_ALIGN(32);
    u32 length;
    {
        MR::ProhibitSchedulerAndInterrupts lock(false);
        GDLObj* previous = __GDCurrentDL;
        GDLObj dl;
        GDInitGDLObj(&dl, buffer, sizeof(buffer));
        __GDCurrentDL = &dl;
        GDSetChanAmbColor(GX_COLOR0A0, *MR::getLightAmbientColor(this));
        GXColor white = {255, 255, 255, 255};
        GDSetChanMatColor(GX_COLOR0A0, white);
        GDSetChanCtrl(GX_COLOR1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_CLAMP, GX_AF_NONE);
        GDPadCurr32();
        length = (u32)(dl.ptr - dl.start);
        __GDCurrentDL = previous;
    }
    mLightDLIndex = (u8)(1 - mLightDLIndex);
    memcpy(mLightDL[mLightDLIndex], buffer, (length + 31) & ~31u);
    mLightDLSize = length;
}

// MarioActor::updateLightDL with the puppet's own area ambient colour and
// white material colour (the directional lights are loaded by draw()).  The
// player's display list until the puppet has an area light.
const u8* RemotePlayerPuppet::lightDL(u32* pSize) const {
    if (mActorLightCtrl == nullptr || mActorLightCtrl->mAreaLightInf == nullptr || mLightDLSize == 0) {
        return playerLightDL(pSize);
    }
    *pSize = mLightDLSize;
    return mLightDL[mLightDLIndex];
}

// A form's body on the puppet's base matrix.  The joints are the body's
// (shared animation matrices), but the skinned vertices need this model's
// own weighted-envelope matrices, which its calc() would make (calcAnmMtx is
// the body's job).
J3DModelX* RemotePlayerPuppet::prepareFormModel(const Form& rForm) const {
    J3DModelX* body = reinterpret_cast< J3DModelX* >(MR::getJ3DModel(this));
    J3DModelX* model = reinterpret_cast< J3DModelX* >(MR::getJ3DModel(rForm.mBody));
    PSMTXCopy(body->getBaseTRMtx(), model->getBaseTRMtx());
    model->setBaseScale(mScale);
    j3dSys.setModel(model);
    model->calcWeightEnvelopeMtx();
    return model;
}

// MarioActor::drawIndirectModel + drawIndirectHand: ice, with its own face.
void RemotePlayerPuppet::drawIndirect() const {
    const Form& ice = mForms[kFormIce];
    if (MR::isDead(this) || mHidden || mMode != 3 || ice.mBody == nullptr || mXanimePlayer == nullptr) {
        return;
    }
    J3DModel* keepModel = j3dSys.getModel();
    J3DTexture* keepTexture = j3dSys.getTexture();
    drawBody(prepareFormModel(ice), true, true);
    if (mFace != nullptr && ice.mHandL != nullptr && ice.mHandR != nullptr) {
        const_cast< RemotePlayerPuppet* >(this)->updateParts();
        ice.mHandL->updateMtx(MR::getJointMtx(this, "HandL"));
        ice.mHandR->updateMtx(MR::getJointMtx(this, "HandR"));
        drawPart(ice.mHandL, mHandPoseL, MR::getJointNum(ice.mHandL) - 1, true);
        drawPart(ice.mHandR, mHandPoseR, MR::getJointNum(ice.mHandR) - 1, true);
    }
    j3dSys.setModel(keepModel);
    j3dSys.setTexture(keepTexture);
}

// MarioActor::drawMarioModel + drawHand + the face.
void RemotePlayerPuppet::draw() const {
    if (MR::isDead(this) || mHidden) {
        return;
    }
    // j3dSys keeps the last drawn model and texture table; the local
    // player's next material update (MarioActor::calcAnim -> updateDL ->
    // J3DTevBlock::diffTexNo) reads texture numbers through it, and a puppet
    // part's smaller table there crashed it (seen in boo mode).  Put back
    // what the scene had.
    struct J3DSysKeep {
        J3DSysKeep() : mModel(j3dSys.getModel()), mTexture(j3dSys.getTexture()) {}
        ~J3DSysKeep() {
            j3dSys.setModel(mModel);
            j3dSys.setTexture(mTexture);
        }
        J3DModel* mModel;
        J3DTexture* mTexture;
    } keep;
    // This puppet's light: its area's lights into GX (the scene loaded the
    // local player's for the whole DrawType_Player list; restored below),
    // and its ambient colour into a display list for the models.
    bool ownLight = mActorLightCtrl != nullptr && mActorLightCtrl->mAreaLightInf != nullptr && mLightDL[0] != nullptr;
    if (ownLight) {
        // Exactly the player's light set-up (area light + point lights),
        // with this puppet's controller standing in for the player's.
        LightFunction::registerPlayerLightCtrl(mActorLightCtrl);
        MR::loadLightPlayer();
    }
    drawModels();
    if (ownLight) {
        LightFunction::registerPlayerLightCtrl(MarioAccess::getPlayerActor()->mActorLightCtrl);
        MR::loadLightPlayer();
    }
}

void RemotePlayerPuppet::drawModels() const {
    J3DModelX* body = reinterpret_cast< J3DModelX* >(MR::getJ3DModel(this));
    if (mMode == 6 && mTeresa != nullptr) {
        return;  // boo: only the Teresa part shows (MarioActor::isAllHidden)
    }
    int formIndex = formForMode(mMode);
    const Form* form = formIndex >= 0 && mForms[formIndex].mBody != nullptr ? &mForms[formIndex] : nullptr;
    if (formIndex == kFormIce && form != nullptr) {
        return;  // drawIndirect (MarioActor::drawMarioModel skips model 3)
    }
    bool ownFace = formIndex == kFormRainbow;
    if (form != nullptr) {
        drawBody(prepareFormModel(*form), ownFace, formIndex != kFormRainbow);  // rainbow: no light DL (drawMarioModel)
    } else {
        J3DModelX* model = body;
        HideFlags keep(model->getModelData());
        if (mEyelidMaterial != 0xFFFF && mEyelidDL[mEyelid & 3] != nullptr) {
            model->_1C8[mEyelidMaterial] = mEyelidDL[mEyelid & 3];  // MarioActor::swapTexture
            model->_1CC[mEyelidMaterial] = mEyelidDLSize[mEyelid & 3];
        }
        drawBody(model, false, true);
    }
    if (mFace != nullptr && mXanimePlayer != nullptr) {
        const_cast< RemotePlayerPuppet* >(this)->updateParts();
        if (form != nullptr && form->mHandL != nullptr && form->mHandR != nullptr) {
            // Rainbow and ice gloves (MarioActor::updateHand picks the set).
            form->mHandL->updateMtx(MR::getJointMtx(this, "HandL"));
            form->mHandR->updateMtx(MR::getJointMtx(this, "HandR"));
            drawPart(form->mHandL, mHandPoseL, MR::getJointNum(form->mHandL) - 1, formIndex != kFormRainbow);
            drawPart(form->mHandR, mHandPoseR, MR::getJointNum(form->mHandR) - 1, formIndex != kFormRainbow);
        } else if (formIndex != kFormSpring) {  // spring Mario has no hands out
            drawPart(mHandL, mHandPoseL, MR::getJointNum(mHandL) - 1, true);
            drawPart(mHandR, mHandPoseR, MR::getJointNum(mHandR) - 1, true);
        }
        if (!ownFace) {
            drawPart(mFace, mFacePose, 8, true);
        }
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
    sLocalTint->set(net::puppetTint(tint ? (u8)atoi(tint) : 0, net::netLocalId()),
                    ModelTint::suitForMode((u8)MarioAccess::getPlayerActor()->mPlayerMode));
}

// Spawn spacing: players arriving at the same start point would stand
// inside each other.  On the first grounded, cutscene-free frame of each
// scene, when anyone else is in the room, the local Mario steps aside by a
// slot fixed by player id (1: stays, 2: +1, 3: -1, 4: +2, 5: -2, times
// kSpawnSpacing along his side vector), so simultaneous arrivals never pick
// the same spot.  Only onto floor within reach and with no wall in between
// (else the mirrored slot, else stay).
const f32 kSpawnSpacing = 100.0f;
bool sSpawnPending = false;
int sSpawnWaitFrames = 0;

bool trySpawnAt(const TVec3f& rFrom, const TVec3f& rUp, const TVec3f& rTo) {
    TVec3f hit;
    // A wall between the spawn point and the slot, at knee height.
    TVec3f knee = rFrom + rUp * 50.0f;
    if (MR::getFirstPolyOnLineToMap(&hit, nullptr, knee, (rTo + rUp * 50.0f) - knee)) {
        return false;
    }
    // Floor under the slot, within 150 units up or down.
    TVec3f top = rTo + rUp * 150.0f;
    if (!MR::getFirstPolyOnLineToMap(&hit, nullptr, top, rUp * -300.0f)) {
        return false;
    }
    MR::setPlayerPos(hit);
    return true;
}

void trySpawnSpacing() {
    if (!sSpawnPending) {
        return;
    }
    if (++sSpawnWaitFrames > 60 * 30) {
        sSpawnPending = false;  // never settled (falling, swimming): leave it
        return;
    }
    if (!MR::isExistMario() || !MR::isOnGroundPlayer() || MR::isDemoActive() || net::netLocalId() == 0) {
        if (getenv("PETARI_PUPPETLOG") && sSpawnWaitFrames % 120 == 1) {
            port_log("puppet: spawn spacing waiting: mario %d ground %d demo %d id %d", MR::isExistMario(),
                     MR::isExistMario() && MR::isOnGroundPlayer(), MR::isDemoActive(), net::netLocalId());
        }
        return;
    }
    sSpawnPending = false;
    bool others = false;
    for (int i = 0; i < net::kMaxRemotes; i++) {
        others |= net::netRemote(i).present;
    }
    int id = net::netLocalId();
    int slot = id <= 1 ? 0 : (id % 2 == 0 ? id / 2 : -(id / 2));
    if (!others || slot == 0) {
        return;
    }
    TVec3f pos = *MR::getPlayerPos();
    TVec3f up, side;
    MR::getPlayerUpVec(&up);
    MR::getPlayerSideVec(&side);
    for (int attempt = 0; attempt < 2; attempt++) {
        f32 offset = kSpawnSpacing * (attempt == 0 ? slot : -slot);
        if (trySpawnAt(pos, up, pos + side * offset)) {
            port_log("puppet: spawn spacing: player %d moved %.0f to the side", id, offset);
            return;
        }
    }
    port_log("puppet: spawn spacing: player %d has no room to the side; staying", id);
}

// Shows the slot's puppet for `luigi` posed by `pose`, hides the other.
// SMG_CONTACT=0: puppets are ghosts again (no push, no stomp, no flinch).
bool contactEnabled() {
    static const char* sContact = getenv("SMG_CONTACT");
    return !(sContact != nullptr && sContact[0] == '0');
}

// Contacts from other players.  Jumped on: the local Mario flinches
// (Mario::doFlipWeak: knocked back, no damage).  Ground-pounded: squashed
// flat like under a crusher (Mario::requestCrush -> MarioCrush: one health,
// three seconds flat, then the usual invincibility).  Both are refused while
// invincible or in a state that cannot be hit.
void applyContacts() {
    net::Contact c;
    while (net::netPollContact(&c)) {
        if (!contactEnabled() || !MR::isExistMario()) {
            continue;
        }
        MarioActor* actor = MarioAccess::getPlayerActor();
        if (c.kind == net::kContactTrample) {
            TVec3f dir(c.dir[0], c.dir[1], c.dir[2]);
            bool flinched = actor->mMario->doFlipWeak(dir);
            port_log("puppet: stomped by player %u%s", c.playerId, flinched ? "" : " (no reaction in this state)");
        } else if (c.kind == net::kContactHipDrop) {
            actor->mMario->requestCrush();
            port_log("puppet: ground-pounded by player %u: squashed", c.playerId);
        }
    }
}

void showSlot(int slot, bool luigi, const NetPlayerCapture& pose, bool visible, u8 tint, bool solid, u8 remoteId) {
    for (int c = 0; c < 2; c++) {
        RemotePlayerPuppet* p = sPuppets[slot][c];
        if (p == nullptr) {
            continue;
        }
        bool mine = (c == 1) == luigi;
        if (mine) {
            p->setTint(tint, ModelTint::suitForMode((u8)((pose.flags & NET_CAPTURE_MODE_MASK) >> NET_CAPTURE_MODE_SHIFT)));
        }
        p->applyState(pose, mine && visible, mine && solid, remoteId);
    }
}
}  // namespace

namespace {
std::atomic<int> sDebugMode{-1};

// PETARI_DEBUG_MODES and the F-key requests, applied on the game thread.
// Forms with their own model need its archive in this stage (see
// SceneDataInitializer); MarioActor skips a form it has no model for.
void applyDebugModes() {
    static const char* sSchedule = getenv("PETARI_DEBUG_MODES");
    static int sDone = 0;
    if (sSchedule != nullptr && *sSchedule != 0) {
        double now = port_host_time_ns() / 1e9;
        static double sStart = now;
        int index = 0;
        for (const char* p = sSchedule; p != nullptr && *p != 0; index++) {
            double t;
            int mode;
            if (sscanf(p, "%lf:%d", &t, &mode) != 2) {
                break;
            }
            if (index >= sDone) {
                if (now - sStart < t) {
                    break;
                }
                sDebugMode = mode;
                sDone++;
            }
            p = strchr(p, ',');
            p = p != nullptr ? p + 1 : nullptr;
        }
    }
    static int sPending = -1;  // a form to enter once back to normal
    int mode = sDebugMode.exchange(-1);
    if (mode < 0) {
        mode = sPending;
        if (mode < 0) {
            return;
        }
    }
    if (!MR::isExistMario()) {
        return;
    }
    if (mode == 101) {  // F10: defeat the nearest enemy (world sync test)
        sPending = -1;
        const TVec3f& p = *MR::getPlayerPos();
        port_world_debug_kill_nearest(p.x, p.y, p.z);
        return;
    }
    if (mode == 100) {  // F9: squash the local player (as a ground pound does)
        sPending = -1;
        port_log("puppet: debug: squash");
        MarioAccess::getPlayerActor()->mMario->requestCrush();
        return;
    }
    {
        // Form to form directly (say spring to boo) is not a path the game
        // takes, and it overruns the old form model's material display
        // lists during the transform flicker (MarioActor::calcAnim diffs the
        // displayed form model with the base model's material flags):
        // heap corruption, crashing later.  Go back to normal first.
        MarioActor* actor = MarioAccess::getPlayerActor();
        bool settled = actor->_3D8 == 0 && actor->mCurrModel == 0;
        if (mode != 0 && actor->mPlayerMode != 0 && actor->mPlayerMode != mode) {
            sPending = mode;
            mode = 0;
        } else if (mode != 0 && actor->mPlayerMode == 0 && !settled) {
            sPending = mode;  // still flickering back to normal: wait
            return;
        } else {
            sPending = -1;
        }
        if (mode == (int)actor->mPlayerMode) {
            return;
        }
    }
    MarioActor* actor = MarioAccess::getPlayerActor();
    // setPlayerMode uses the form's model holder unchecked.
    const void* holder = mode == 1 ? (const void*)actor->_9C8 : mode == 3 ? (const void*)actor->_9C0 : mode == 4 ? (const void*)actor->_9E4
                       : mode == 5 ? (const void*)actor->_A00 : mode == 6 ? (const void*)actor->_9A4 : (const void*)actor;
    if (mode > 9 || holder == nullptr) {
        port_log("puppet: debug: player mode %d has no model in this stage", mode);
        return;
    }
    // The first time per form plays a camera aimed at the collected item
    // (MarioActor::calcAnimInMovement, _4A4), which a debug switch has none
    // of, and a message that waits for A: mark the firsts as seen.
    static const char* const kFirsts[] = {
        "\x83\x6e\x83\x60\x83\x7d\x83\x8a\x83\x49\x8f\x89\x95\xcf\x90\x67",              // bee
        "\x83\x65\x83\x8c\x83\x54\x83\x7d\x83\x8a\x83\x49\x8f\x89\x95\xcf\x90\x67",      // boo
        "\x83\x7a\x83\x62\x83\x70\x81\x5b\x83\x7d\x83\x8a\x83\x49\x8f\x89\x95\xcf\x90\x67",  // spring
        "\x83\x74\x83\x40\x83\x43\x83\x41\x83\x7d\x83\x8a\x83\x49\x8f\x89\x95\xcf\x90\x67",  // fire
        "\x83\x41\x83\x43\x83\x58\x83\x7d\x83\x8a\x83\x49\x8f\x89\x95\xcf\x90\x67",      // ice
        "\x83\x74\x83\x89\x83\x43\x83\x93\x83\x4f\x83\x7d\x83\x8a\x83\x49\x8f\x89\x95\xcf\x90\x67",  // flying
        "\x96\xb3\x93\x47\x83\x7d\x83\x8a\x83\x49\x8f\x89\x95\xcf\x90\x67",                    // rainbow
    };
    for (const char* first : kFirsts) {
        GameDataFunction::onGameEventFlag(first);
    }
    port_log("puppet: debug: player mode %d", mode);
    actor->setPlayerMode((u16)mode, true);
}
}  // namespace

extern "C" void port_net_debug_request_mode(int mode) { sDebugMode = mode; }

extern "C" int port_net_wants_puppets(void) { return loopbackDelay() != nullptr || net::netEnabled(); }

extern "C" void port_net_puppets_create_for_scene(void) {
    if (!port_net_wants_puppets() || MR::isStageFileSelect()) {
        return;  // not on the title / file-select stage
    }
    int count = net::netEnabled() ? net::kMaxRemotes : 1;
    forgetPuppets();
    JKRHeap* heap = JKRHeap::getCurrentHeap();
    s32 freeBefore = heap->getTotalFreeSize();
    for (int i = 0; i < count; i++) {
        for (int c = 0; c < 2; c++) {
            // The name keeps the race ghost's (ゴーストマリオ): see the header.
            sPuppets[i][c] = new RemotePlayerPuppet(c ? "\x83\x53\x81\x5b\x83\x58\x83\x67\x83\x7d\x83\x8a\x83\x49 (co-op Luigi)"
                                                      : "\x83\x53\x81\x5b\x83\x58\x83\x67\x83\x7d\x83\x8a\x83\x49 (co-op Mario)",
                                                    c == 1);
            sPuppets[i][c]->initWithoutIter();
        }
    }
    if (net::netEnabled()) {
        // Same model data as the player's (the puppet of its character).
        sLocalTint = new ModelTint();
        sLocalTint->init(MR::getJ3DModelData(sPuppets[0][MR::isPlayerLuigi() ? 1 : 0]), MR::isPlayerLuigi());
    }
    sAdoptOnNextScene = true;
    {
        const char* spacing = getenv("SMG_SPAWN_SPACING");  // "0": off
        sSpawnPending = net::netEnabled() && !(spacing != nullptr && spacing[0] == '0');
    }
    sSpawnWaitFrames = 0;
    port_log("puppet: %d Mario + %d Luigi created for the new scene: %d KB of the current heap, %d KB left", count, count,
             (int)((freeBefore - heap->getTotalFreeSize()) / 1024), (int)(heap->getTotalFreeSize() / 1024));
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
    applyDebugModes();
    NetPlayerCapture now;
    bool haveLocal = net_capture_latest(&now) != 0;

    if (net::netEnabled()) {
        net::netTick(haveLocal ? &now : nullptr);
        applyContacts();
        port_world_update();
        trySpawnSpacing();
        updateLocalTint();
        for (int i = 0; i < net::kMaxRemotes; i++) {
            const net::RemoteView& v = net::netRemote(i);
            showSlot(i, v.luigi, v.pose, v.visible, net::puppetTint(v.tint, v.id), contactEnabled() && !v.frozen, v.id);
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
        // PETARI_LOOPBACK_SIDE=<units>: stand the copy that far to the
        // player's side (baseMtx column 0), e.g. "PETARI_LOOPBACK=0
        // PETARI_LOOPBACK_SIDE=120" for a side-by-side comparison.
        static const char* sSide = getenv("PETARI_LOOPBACK_SIDE");
        if (sSide != nullptr && *sSide != 0) {
            float side = (float)atof(sSide);
            for (int i = 0; i < 3; i++) {
                past.pos[i] += past.baseMtx[i][0] * side;
                past.baseMtx[i][3] = past.pos[i];
            }
        }
        // SMG_TINT picks the copy's colour, otherwise as player 2 (green).
        const char* tint = getenv("SMG_TINT");
        // The copy is solid only with a delay (a copy inside the player would
        // push him forever).
        showSlot(0, (past.flags & NET_CAPTURE_LUIGI) != 0, past, valid, net::puppetTint(tint ? (u8)atoi(tint) : 0, 2),
                 contactEnabled() && delayNs > 0, 0);
    }
}
