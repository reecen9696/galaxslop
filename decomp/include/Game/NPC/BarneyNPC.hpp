#pragma once

#if defined(TARGET_PC) || defined(TARGET_WEB)
#include "Game/LiveActor/LiveActor.hpp"

class ActorLightCtrl;
class BarneyBalloon;

// Barney, a dog the port adds to the first area of a new game (the Star
// Festival, PeachCastleGardenGalaxy).  His model and animations are the
// port's own (assets/mods/ObjectData/Barney.arc, cooked into the build), not
// disc data, and he has no placement in the stage files: SceneDataInitializer
// mounts his archive and creates him (BarneyNPC::requestArchive, create).
//
// He runs loose laps of the paved plaza around its flowerbed, kicking up
// Mario's own running dust (MarioActor::playCommonEffect's 共通砂煙レベル).
// When a player comes close he stops, faces them and says the next of his
// lines in the game's own short talk balloon (as the Toads do) with a bark
// (port/sfx.h); E starts the next conversation (port/talk.h).  In co-op one
// player's game runs him and the others follow it (net/world/actor_sync),
// lines included.
class BarneyNPC : public LiveActor {
public:
    BarneyNPC(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);

    // Scene hooks (SceneDataInitializer, TARGET_PC): whether this stage has
    // Barney, mounting his archive with the stage's, and creating him.
    static bool isPlacedInCurrentStage();
    static void requestArchive();
    static void create();

private:
    void startAnim(const char* pName);
    void updateRun();
    void updateStop(const TVec3f& rNearest);
    void updateTalk(const TVec3f& rNearest);
    void updateCopy(const char* pOwnerAnim);
    void say(const char* pWhy);
    f32 nearestPlayer(TVec3f* pPos) const;
    void playRunSmoke();
    TVec3f up() const;

    TVec3f mFront;
    const char* mAnim;
    s32 mTalkFrames;  // frames left of the bark animation
    bool mStopped;    // Mario is close
    ActorLightCtrl* mPendingLight;
    BarneyBalloon* mBalloon;
    f32 mLapRadius;     // this stretch of his lap
    f32 mRunSpeed;
    s32 mWanderFrames;  // until the next change of radius and pace
    s32 mPauseFrames;   // stopped to look around
    s32 mBarkSound;     // port/sfx.h
    bool mTalked;       // has said a line since a player stopped him
    TVec3f mLastPos;
    char mCopyAnim[16]; // the animation the game running him played last (co-op)
};
#endif
