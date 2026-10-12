// A remote player drawn in the local game (P2-05, D-018): Mario's model and
// animations, posed from received state, interacting with nothing.
//
// Modelled on GhostPlayer (decomp/src/Game/Player/GhostPlayer.cpp, the
// Cosmic Mario race ghost): same model archive, the player's own animation
// table, a full-body and an upper-body XanimePlayer, the player draw path.
// Everything race-specific (packets from disc, demos, power star, sensors,
// camera) is left out.
#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "net/capture/player_capture.h"

class XanimePlayer;

class RemotePlayerPuppet : public LiveActor {
public:
    RemotePlayerPuppet(const char* pName, bool luigi);

    virtual void init(const JMapInfoIter& rIter);
    virtual void appear();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void draw() const;

    // Pose for this frame; `valid` false hides the puppet.
    void applyState(const NetPlayerCapture& rState, bool valid);

    XanimePlayer* mXanimePlayer;
    XanimePlayer* mXanimePlayerUpper;
    Mtx mTargetRotationMtx;
    u32 mAnimHash;
    bool mLuigi;
    bool mHidden;
};

// Per-scene management, called from GameScene::update (TARGET_PC) every
// gameplay frame.  PETARI_LOOPBACK=<seconds>: one puppet replays the local
// player's own capture that many seconds late (no network).
extern "C" void port_net_puppets_update(void* pScene);
