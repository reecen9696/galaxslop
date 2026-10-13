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
#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "net/capture/player_capture.h"

class J3DModelData;
class J3DModelX;
class ModelHolder;
class XanimePlayer;

// Recolourable shirt and cap for a player-model instance (puppet_tint.h):
// a private copy of each texture plus a display list binding it, hooked in
// through J3DModelX's per-material DL (the one MarioActor::swapTexture uses
// for blinking).  Buffers come from the current heap, so init() runs during
// scene initialisation; install() and set() are cheap and run any time.
class ModelTint {
public:
    ModelTint();
    void init(J3DModelData* pData, bool luigi);
    void install(J3DModelX* pModel) const;
    void set(u8 tint);

private:
    struct Texture {
        const u8* mSource;
        u8* mCopy;
        u32 mSize;
        u16 mMaterialNo;
        u8* mDL;
        u16 mDLSize;
    };
    void initTexture(Texture* pTex, J3DModelData* pData, const char* pMaterialName);
    Texture mTextures[2];
    bool mLuigi;
    u8 mTint;
};

class RemotePlayerPuppet : public LiveActor {
public:
    RemotePlayerPuppet(const char* pName, bool luigi);

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void draw() const;

    void initAnimation();
    // Pose for this frame; `valid` false hides the puppet.
    void applyState(const NetPlayerCapture& rState, bool valid);
    // Clothes colour (net::kTint*); cheap when unchanged.
    void setTint(u8 tint) { mTint.set(tint); }

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
    void updateParts();
    ModelHolder* initPart(const char* pName, const char* pBodyJoint, u32 packets);
    void drawPart(ModelHolder* pPart, u8 pose, u32 poses) const;
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
