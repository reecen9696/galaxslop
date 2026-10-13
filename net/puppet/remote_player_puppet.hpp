// A remote player drawn in the local game (P2-05, D-018): Mario's or Luigi's
// model and animations, posed from received state, interacting with nothing.
//
// Modelled on GhostPlayer (decomp/src/Game/Player/GhostPlayer.cpp, the
// Cosmic Mario race ghost): the player's own animation table, a full-body
// and an upper-body XanimePlayer, the player draw path.  Everything
// race-specific (packets from disc, demos, power star, sensors, camera) is
// left out.  Drawn like the local player rather than the ghost: the body
// model (Mario.arc / Luigi.arc) plus separate face and hand models whose
// expression and hand poses follow the animation (MarioActor::initFace,
// initHand, updateFace, updateHand), lit with the player's light display
// list (MarioActor::updateLightDL), with the shirt and cap textures copied
// per puppet so each can be tinted (puppet_tint.h).
//
// Power-up forms follow the sender's PlayerMode: fire and flying are suit
// textures (ModelTint); rainbow, ice, bee and spring swap the body for that
// form's model, which has the same skeleton and so shares the body's
// animated joints (J3DModelX::copyAnmMtxBuffer, as MarioActor does for its
// ice and rainbow models), with the form's own hands and extras (rainbow
// colour cycle, bee wings); boo draws the Teresa model instead.
//
// Touch (D-020, option C): the race ghost's Ride sensor, active only while
// the puppet is shown and receiving.  It pushes the local Mario out (each
// client pushes its own Mario, so pushing is mutual), and the local Mario
// bounces off it when stomping, which also sends a Contact so the other
// player flinches.  The ghost's star bits are left out.  The actor name
// contains the ghost's (ゴーストマリオ) so MarioActor::receiveMsgPush keeps
// its exemption for a jumping or hanging Mario.  SMG_CONTACT=0 turns it off.
#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "net/capture/player_capture.h"

class J3DModelData;
class J3DModelX;
class ActorLightCtrl;
class ModelHolder;
class PartsModel;
class XanimePlayer;

// Recolourable shirt and cap for a player-model instance (puppet_tint.h):
// a private copy of each texture plus a display list binding it, hooked in
// through J3DModelX's per-material DL (the one MarioActor::swapTexture uses
// for blinking).  The copy is made from one of the three suit variants
// (MarioBody.0/.1/.2, MarioCap.0/.1/.2: normal, flying, fire), which the
// player otherwise selects with its "ColorChange" btp; the DL replaces that
// choice, so the variant has to come through here.  Buffers come from the
// current heap, so init() runs during scene initialisation; install() and
// set() are cheap and run any time.
class ModelTint {
public:
    ModelTint();
    void init(J3DModelData* pData, bool luigi);
    void install(J3DModelX* pModel) const;
    void set(u8 tint, u8 suit);
    // ColorChange frame for a PlayerMode: 2 fire, 1 flying, else 0.
    static u8 suitForMode(u8 mode) { return mode == 2 ? 2 : mode == 7 ? 1 : 0; }

private:
    struct Texture {
        const u8* mSource[3];  // per suit
        u8* mCopy;
        u32 mSize;
        u16 mMaterialNo;
        u8* mDL;
        u16 mDLSize;
    };
    void initTexture(Texture* pTex, J3DModelData* pData, const char* pMaterialName, const char* pTexturePrefix);
    Texture mTextures[2];
    bool mLuigi;
    u8 mTint;
    u8 mSuit;
};

class RemotePlayerPuppet : public LiveActor {
public:
    RemotePlayerPuppet(const char* pName, bool luigi);

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual void draw() const;
    // Ice is drawn in the refraction pass (MarioActor::drawIndirectModel).
    void drawIndirect() const;
    void drawModels() const;

    void initAnimation();
    // Pose for this frame; `valid` false hides the puppet.  `solid`: the
    // sensor is on (shown, fresh data, contact enabled).  `remoteId`: the
    // player it shows (0: loopback).
    void applyState(const NetPlayerCapture& rState, bool valid, bool solid = false, u8 remoteId = 0);
    // Clothes colour (net::kTint*); cheap when unchanged.
    void setTint(u8 tint, u8 suit);

    XanimePlayer* mXanimePlayer;
    XanimePlayer* mXanimePlayerUpper;
    Mtx mTargetRotationMtx;
    u32 mAnimHash;
    bool mLuigi;
    bool mHidden;
    ModelTint mTint;
    ModelHolder* mFace;
    ModelHolder* mHandL;
    ModelHolder* mHandR;

    // Body-swapping forms, by PlayerMode (1 rainbow, 3 ice, 4 bee, 5 spring);
    // null where the stage has no model for it.
    enum { kFormRainbow, kFormIce, kFormBee, kFormSpring, kFormCount };
    struct Form {
        ModelHolder* mBody;
        ModelHolder* mHandL;  // own hands (rainbow, ice); null: the normal ones
        ModelHolder* mHandR;
        ModelTint mTint;      // bee and spring keep Mario's shirt and cap
        bool mTinted;
    };
    Form mForms[kFormCount];
    // Drawn by the scene as parts that follow a matrix (MarioActor's
    // MarioParts): the wings on Spine2, the boo on the base matrix.
    PartsModel* mBeeWing;
    PartsModel* mTeresa;  // boo
    u8 mMode;              // sender's PlayerMode
    u8 mRemoteId;
    bool mSolid;
    u8 mTrampleCooldown;   // frames
    // Own light (MarioActor::calcFogLighting / updateLightDL, without the
    // damage and item fog): the area light where the puppet stands, from its
    // own ActorLightCtrl, rebuilt each draw; double-buffered like the player's.
    u8* mLightDL[2];
    u32 mLightDLSize;
    u8 mLightDLIndex;
    ActorLightCtrl* mPendingLight;
    bool mAirborne;
    // Expression and hand poses this frame (joint index - 1 in the face and
    // hand models), from the animation's PartsControl joint.
    u8 mFacePose;
    u8 mHandPoseL;
    u8 mHandPoseR;
    // Eyelid textures (mario_eyeLid.0..3) bound per frame through the
    // EyeLid_v material's DL hook (MarioActor::swapTextureInit/swapTexture).
    u8* mEyelidDL[4];
    u16 mEyelidDLSize[4];
    u16 mEyelidMaterial;
    u8 mEyelid;

private:
    const u8* lightDL(u32* pSize) const;
    void updateLightDL();
    static int formForMode(u8 mode);
    void initForms();
    void showParts(bool wing, bool teresa);
    ModelHolder* initFormModel(const char* pName, bool shareJoints);
    J3DModelX* prepareFormModel(const Form& rForm) const;
    void drawBody(J3DModelX* pModel, bool ownFace, bool lit) const;
    void updateParts();
    ModelHolder* initPart(const char* pName, const char* pBodyJoint, u32 packets);
    void drawPart(ModelHolder* pPart, u8 pose, u32 poses, bool lit) const;
};

// Per-scene management, called from GameScene::update (TARGET_PC) every
// gameplay frame.  PETARI_LOOPBACK=<seconds>: one puppet replays the local
// player's own capture that many seconds late (no network).
extern "C" void port_net_puppets_update(void* pScene);

// Creates this scene's puppets; called during scene initialisation
// (SceneDataInitializer, TARGET_PC), where actors may be created.
extern "C" void port_net_puppets_create_for_scene(void);

// Whether this session draws remote players, so stage loading also mounts
// the puppet models (SceneDataInitializer, TARGET_PC).
extern "C" int port_net_wants_puppets(void);

// Debug: put the local player into PlayerMode `mode` on the next gameplay
// frame (0 normal, 1 rainbow, 2 fire, 3 ice, 4 bee, 5 spring, 6 boo,
// 7 flying).  Any thread.  The desktop window maps F1-F8 to it;
// PETARI_DEBUG_MODES="seconds:mode,..." schedules them (seconds after the
// first gameplay frame).
extern "C" void port_net_debug_request_mode(int mode);
