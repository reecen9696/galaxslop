#if defined(TARGET_PC) || defined(TARGET_WEB)
// See BarneyNPC.hpp.
#include "Game/NPC/BarneyNPC.hpp"
#include "Game/Effect/MultiEmitter.hpp"
#include "Game/LiveActor/ActorLightCtrl.hpp"
#include "Game/LiveActor/EffectKeeper.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/NPC/TalkTextFormer.hpp"
#include "Game/Screen/LayoutActor.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "port/port.h"
#include "net/client/net_client.h"
#include "net/world/actor_sync.h"
#include "port/sfx.h"
#include "port/talk.h"
#include <JSystem/JKernel/JKRSolidHeap.hpp>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {
    const char* const cStage = "PeachCastleGardenGalaxy";

    // The plaza in front of Mario's first start (StartInfo MarioNo 0 at
    // 13550, 1300, 11900), from the stage's collision: a raised flowerbed
    // (rim at radius ~320) in a paved ring out to a low curb at ~575-700,
    // then open paving at 1300 out to ~1334, where the edge rises.  He laps
    // the open paving anticlockwise (seen from above), drifting in and out
    // and changing pace now and then, with the odd stop to look around.
    const TVec3f cFlowerbed(12676.0f, 1300.0f, 11413.0f);
    const f32 cLapRadiusMin = 780.0f;
    const f32 cLapRadiusMax = 1150.0f;
    const f32 cLapLeadDegree = 25.0f;  // steers at the lap this far ahead of him

    const f32 cScale = 1.0f;           // the model is ~190 tall; Mario ~160
    const f32 cRunSpeedMin = 6.5f;     // Mario runs at about 13
    const f32 cRunSpeedMax = 9.5f;
    const f32 cTurnDegree = 8.0f;      // per frame
    const f32 cStopNear = 300.0f;      // a player closer than this: stop, look, talk
    const f32 cStopFar = 420.0f;       // ...until they are further than this
    const f32 cHearRadius = 1500.0f;   // the balloon and bark reach a player this close
    const f32 cBalloonHeight = 125.0f;
    const s32 cTalkFrames = 120;       // talk.bck

    // What he says, one per conversation, in order, then from the top again;
    // the same line on every player's screen (see updateCopy).  Two lines
    // where one would be too wide for the balloon.
    const wchar_t* const cLines[] = {
        L"Woof woof someone left the door open\nI'm going exploring",
        L"I want to bite someone\nwoof woof",
        L"Woof woof I have zoomies",
        L"I just ate Paris's sock",
        L"Is that a Star Bit?\nCan I eat it?",
        L"Woof I just ate a mushroom\noff the ground",
        L"Grrrrrr woof! Nick locked me\nout of his room",
        L"Woof woof I barked at a Luma\nand now it won't stop crying",
        L"I licked a Goomba woof woof",
        L"Woof Nick took my spot on the couch\nso I'm sitting on Paris's face",
        L"That Chain Chomp looked at me\nwoof woof I'm going to fight it",
    };
    const s32 cLineNum = sizeof(cLines) / sizeof(cLines[0]);
    s32 sNextLine = 0;  // for the whole session, across stage visits

    // Co-op actor sync's id for him (placed actors use a hash of their name
    // and placement; he has none): FNV-1a of "BarneyNPC".
    const u32 cSyncId = 0xf3f1b581u;

    // MarioActor's running dust: cSmokeTable's 共通砂煙レベル, which on plain
    // ground plays SmokeSphereFollow1Time each running frame at Mario's
    // feet, scaled 0.65 (MarioActor::initCommonEffect, playCommonEffect);
    // smaller here, as he is.
    const char* const cRunSmokeName = "\x8b\xa4\x92\xca\x8d\xbb\x89\x8c\x83\x8c\x83\x78\x83\x8b";
    const char* const cRunSmokeEffect = "SmokeSphereFollow1Time";
    const f32 cRunSmokeScale = 0.7f;
}  // namespace

// The Toads' short talk balloon (TalkBalloonShort: the TalkBalloonStretch
// layout, its Appear / OneLine / End animations and the beak that points at
// the speaker), showing a line of our own: TalkBalloonShort takes its text
// from a TalkMessageCtrl, i.e. from the disc's messages, which have none for
// Barney.
class BarneyBalloon : public LayoutActor {
public:
    BarneyBalloon() : LayoutActor("Barney balloon", true), mText(nullptr), mHalfWidth(0.0f), mClosing(false), mBeakMoving(false) {}

    virtual void init(const JMapInfoIter&) {
        MR::connectToSceneLayout(this);
        initLayoutManager("TalkBalloonStretch", 2);
        MR::createAndAddPaneCtrl(this, "Balloon", 1);
        mText = new TalkTextFormer(this, "Text00");
        MR::hideScreen(this);
        kill();
    }

    void open(const wchar_t* pLine, const TVec3f& rAnchor) {
        mAnchor = rAnchor;
        mClosing = false;
        LayoutActor::appear();
        MR::showScreen(this);
        MR::startAnim(this, "Appear", 0);
        MR::startPaneAnim(this, "Balloon", "Beak", 0);
        mText->formMessage(pLine, 0);
        MR::startAnim(this, MR::countMessageLine(pLine) == 1 ? "OneLine" : "TwoLine", 1);
        MR::setAnimFrameAndStopAdjustTextWidth(this, "TxtText", 1);
        mHalfWidth = MR::getAnimFrame(this, 1) * 0.5f + 40.0f;  // the text's width, and the balloon's edge
        follow(rAnchor);
    }

    void close() {
        if (!mClosing && !MR::isDead(this)) {
            mClosing = true;
            MR::startAnim(this, "End", 0);
        }
    }

    bool isOpen() const { return !MR::isDead(this) && !mClosing; }

    void follow(const TVec3f& rAnchor) { mAnchor = rAnchor; }

    virtual void control() {
        // TalkBalloonShort::updateBalloon, from the speaker's screen position.
        TVec3f screen;
        MR::calcScreenPosition(&screen, mAnchor);
        if (screen.z > 0.0f) {
            MR::hideScreen(this);
        } else {
            MR::showScreen(this);
        }
        // Kept on screen (a long line near the edge would run off it); the
        // beak still turns to him.
        f32 x = screen.x;
        f32 lo = mHalfWidth + 8.0f, hi = MR::getScreenWidth() - mHalfWidth - 8.0f;
        if (lo < hi) x = x < lo ? lo : x > hi ? hi : x;
        setTrans(TVec2f(x, screen.y));
        updateBeak(screen);
        if (mClosing) {
            if (MR::isAnimStopped(this, 0)) {
                kill();
            }
        } else {
            mText->updateTalking();
        }
    }

private:
    // TalkBalloon::updateBalloon: the beak turns towards the speaker.
    void updateBeak(const TVec3f& rScreen) {
        TVec2f beak;
        MR::copyPaneTrans(&beak, this, "PicBeak");
        TVec2f dir(rScreen.x - beak.x, rScreen.y - beak.y);
        MR::normalizeOrZero(&dir);
        f32 degree = MR::acos(dir.y) * 57.29578f;
        f32 max = MR::getPaneAnimFrameMax(this, "Balloon", 0);
        f32 frame = MR::getPaneAnimFrame(this, "Balloon", 0);
        f32 want = dir.x > 0.0f ? (max * 0.5f + degree < max ? max * 0.5f + degree : max)
                                : (max * 0.5f - degree > 0.0f ? max * 0.5f - degree : 0.0f);
        if (!mBeakMoving) {
            if (fabsf(frame - want) < 10.0f) {
                return;
            }
            mBeakMoving = true;
        }
        want = frame > want ? frame - 1.0f : frame + 1.0f;
        if (fabsf(frame - want) < 2.0f) {
            mBeakMoving = false;
        }
        MR::setPaneAnimFrameAndStop(this, "Balloon", want, 0);
    }

    TalkTextFormer* mText;
    f32 mHalfWidth;
    TVec3f mAnchor;
    bool mClosing;
    bool mBeakMoving;
};

BarneyNPC::BarneyNPC(const char* pName)
    : LiveActor(pName), mFront(0.0f, 0.0f, 1.0f), mAnim(nullptr), mTalkFrames(0), mStopped(false), mPendingLight(nullptr),
      mBalloon(nullptr), mLapRadius(900.0f), mRunSpeed(8.0f), mWanderFrames(0), mPauseFrames(0), mBarkSound(-1),
      mTalked(false), mLastPos(0.0f, 0.0f, 0.0f) {
    mCopyAnim[0] = 0;
}

bool BarneyNPC::isPlacedInCurrentStage() {
    static const bool sOff = getenv("SMG_BARNEY") != nullptr && getenv("SMG_BARNEY")[0] == '0';
    return !sOff && MR::isEqualStageName(cStage);
}

void BarneyNPC::requestArchive() {
    if (isPlacedInCurrentStage()) {
        MR::mountAsyncArchiveByObjectOrLayoutName("Barney", MR::getSceneHeapGDDR3());
    }
}

void BarneyNPC::create() {
    if (isPlacedInCurrentStage()) {
        BarneyNPC* barney = new BarneyNPC("Barney");
        barney->initWithoutIter();
    }
}

void BarneyNPC::init(const JMapInfoIter& rIter) {
    // Across the plaza from where Mario arrives (13550, 11900), so he is
    // running when Mario first sees him.
    mPosition.set(cFlowerbed.x - 900.0f * 0.707f, cFlowerbed.y + 60.0f, cFlowerbed.z - 900.0f * 0.707f);
    mFront.set(-0.707f, 0.0f, 0.707f);
    mScale.set(cScale, cScale, cScale);
    initModelManagerWithAnm("Barney", nullptr, false);
    MR::connectToSceneNpc(this);
    // MR::initLightCtrl, in two steps: the area lights it looks up don't
    // exist yet while the scene initialises, so it is attached on his first
    // frame (control).
    initActorLightCtrl();
    mPendingLight = mActorLightCtrl;
    mActorLightCtrl = nullptr;
    MR::onCalcGravity(this);
    initBinder(70.0f, 70.0f, 0);
    initEffectKeeper(1, nullptr, false);
    mEffectKeeper->registerEffectWithoutSRT(cRunSmokeEffect, cRunSmokeName);
    initHitSensor(1);
    MR::addHitSensorNpc(this, "body", 8, 75.0f, TVec3f(0.0f, 75.0f, 0.0f));
    MR::initShadowVolumeSphere(this, 75.0f);
    MR::invalidateClipping(this);
    mBalloon = new BarneyBalloon();
    mBalloon->initWithoutIter();
    mBarkSound = port_sfx_load("Bark");
    // Co-op: one player's game runs him (the nearest player's), the others
    // show him as it does (net/world/actor_sync).
    port_actor_sync_track(this, cSyncId);
    // His first animation starts on his first frame (control): one started
    // here, while the scene initialises, left his legs still until the next
    // change of animation.
    makeActorAppeared();
    port_log("barney: in %s, lapping the flowerbed at %.0f %.0f", cStage, cFlowerbed.x, cFlowerbed.z);
}

TVec3f BarneyNPC::up() const {
    TVec3f v(-mGravity.x, -mGravity.y, -mGravity.z);
    if (MR::isNearZero(v)) {
        v.set(0.0f, 1.0f, 0.0f);
    }
    return v;
}

void BarneyNPC::startAnim(const char* pName) {
    if (mAnim == nullptr || strcmp(mAnim, pName) != 0) {
        mAnim = pName;
        MR::startBck(this, pName);
    }
}

void BarneyNPC::control() {
    if (mPendingLight != nullptr) {
        mActorLightCtrl = mPendingLight;
        mPendingLight = nullptr;
        mActorLightCtrl->init(-1, false);
        mAnim = nullptr;
        startAnim("run");
    }

    // Co-op: another player's game runs him; this one shows what it does.
    const char* ownerAnim = nullptr;
    if (port_actor_sync_driven(this, &ownerAnim)) {
        updateCopy(ownerAnim);
        return;
    }
    mCopyAnim[0] = 0;

    // A player coming close stops him; he runs again once they have gone
    // (two distances, so he doesn't flicker at the edge).
    TVec3f nearest;
    f32 dist = nearestPlayer(&nearest);
    mStopped = dist < (mStopped ? cStopFar : cStopNear);
    updateTalk(nearest);
    if (mStopped) {
        updateStop(nearest);
    } else {
        updateRun();
    }

    // SMG_BARNEY_LOG=1: what he is doing, twice a second.
    static const bool sLog = getenv("SMG_BARNEY_LOG") != nullptr;
    static s32 sLogFrame = 0;
    if (sLog && ++sLogFrame % 30 == 0) {
        port_log("barney: anim %s frame %.1f/%.0f rate %.2f stopped %d ground %d speed %.1f pause %d talk %d near %d line %d", mAnim ? mAnim : "-",
                 MR::getBckFrame(this), MR::getBckFrameMax(this), MR::getBckRate(this), MR::isBckStopped(this), MR::isOnGround(this),
                 mVelocity.length(), mPauseFrames, mTalkFrames, mStopped, sNextLine);
    }

    // Gravity every frame, so he follows the plaza's slope down instead of
    // running off it into the air; the ground stops him (the binder), and
    // standing on it ends any fall.
    if (MR::isOnGround(this)) {
        TVec3f h;
        f32 v = MR::vecKillElement(mVelocity, up(), &h);
        mVelocity = h + up() * (v > 0.0f ? v : 0.0f);
    } else {
        MR::attenuateVelocity(this, 0.99f);
    }
    MR::addVelocityToGravity(this, 1.0f);
}

// The nearest player in the stage, this one or (co-op) another, and how far.
f32 BarneyNPC::nearestPlayer(TVec3f* pPos) const {
    *pPos = *MR::getPlayerPos();
    f32 best = MR::isPlayerDead() ? 1e30f : (mPosition - *pPos).length();
    if (net::netEnabled()) {
        for (int i = 0; i < net::kMaxRemotes; i++) {
            const net::RemoteView& v = net::netRemote(i);
            if (!v.present || !v.visible || v.frozen) continue;
            TVec3f p(v.pose.pos[0], v.pose.pos[1], v.pose.pos[2]);
            f32 d = (mPosition - p).length();
            if (d < best) {
                best = d;
                *pPos = p;
            }
        }
    }
    return best;
}

// The next line: the balloon and bark for this player if they are close
// enough to see it.  Every game takes lines in the same order, the one that
// runs him when he starts talking, the others when they see him start
// (updateCopy), so all show the same line.
void BarneyNPC::say(const char* pWhy) {
    s32 index = sNextLine;
    sNextLine = (sNextLine + 1) % cLineNum;
    TVec3f anchor = mPosition + up() * cBalloonHeight;
    if (MR::isNearPlayer(this, cHearRadius) && !MR::isPlayerDead()) {
        mBalloon->open(cLines[index], anchor);
        port_sfx_play(mBarkSound, MR::isNearPlayer(this, cStopFar) ? 0.7f : 0.4f);
    }
    port_log("talk: Barney: line %d of %d (%s)", (int)index + 1, (int)cLineNum, pWhy);
}

// Run by another player's game: placed and animated by actor sync
// (port_actor_sync_movement); here only the balloon, the bark and the dust.
void BarneyNPC::updateCopy(const char* pOwnerAnim) {
    port_talk_take_press();  // E is for the game that runs him
    mVelocity.zero();
    mAnim = nullptr;  // if this game takes him over, it starts its own animation
    bool talking = strcmp(pOwnerAnim, "talk") == 0;
    if (talking && strcmp(mCopyAnim, "talk") != 0) {
        say("seen");
        mTalkFrames = cTalkFrames;
        mTalked = true;
    }
    if (strcmp(pOwnerAnim, "run") == 0) {
        mTalked = false;
    }
    // mTalked and mTalkFrames go on if this game takes him over mid-
    // conversation (the nearest player changed), so it doesn't start
    // another one with the next line.
    snprintf(mCopyAnim, sizeof(mCopyAnim), "%s", pOwnerAnim);
    if (mTalkFrames > 0) {
        mTalkFrames--;
    }
    if (strcmp(pOwnerAnim, "run") == 0 || !MR::isNearPlayer(this, cHearRadius)) {
        mBalloon->close();
    } else {
        mBalloon->follow(mPosition + up() * cBalloonHeight);
    }
    TVec3f moved = mPosition - mLastPos;
    mLastPos = mPosition;
    if (strcmp(pOwnerAnim, "run") == 0 && moved.length() > 1.0f && moved.length() < 50.0f) {
        playRunSmoke();
    }
}

// Steers at the point of his lap a little ahead of where he is, so he keeps
// circling the flowerbed and finds his way back after being pushed off.
// Every few seconds the lap's radius and his pace change at random, and now
// and then he stops for a moment.
void BarneyNPC::updateRun() {
    if (--mWanderFrames <= 0) {
        mLapRadius = MR::getRandom(cLapRadiusMin, cLapRadiusMax);
        mRunSpeed = MR::getRandom(cRunSpeedMin, cRunSpeedMax);
        mWanderFrames = MR::getRandom((s32)120, (s32)300);
        if (MR::getRandom() < 0.2f) {
            mPauseFrames = MR::getRandom((s32)50, (s32)110);
        }
    }
    TVec3f h;
    f32 v = MR::vecKillElement(mVelocity, up(), &h);
    if (mPauseFrames > 0) {
        mPauseFrames--;
        if (mTalkFrames <= 0) {
            startAnim("wait");
        }
        mVelocity = h * 0.8f + up() * v;
        return;
    }
    if (mTalkFrames <= 0) {
        startAnim("run");
    }
    f32 angle = atan2f(mPosition.x - cFlowerbed.x, mPosition.z - cFlowerbed.z) + cLapLeadDegree * 0.017453293f;
    TVec3f target(cFlowerbed.x + mLapRadius * sinf(angle), mPosition.y, cFlowerbed.z + mLapRadius * cosf(angle));
    MR::turnDirectionToTargetUseGroundNormalDegree(this, &mFront, target, cTurnDegree);

    TVec3f want = mFront * mRunSpeed;
    mVelocity = h + (want - h) * 0.25f + up() * v;
    if (MR::isOnGround(this)) {
        playRunSmoke();
    }
}

void BarneyNPC::updateStop(const TVec3f& rNearest) {
    if (mTalkFrames <= 0) {
        startAnim("wait");
    }
    MR::turnDirectionToTargetUseGroundNormalDegree(this, &mFront, rNearest, cTurnDegree);
    TVec3f h;
    f32 v = MR::vecKillElement(mVelocity, up(), &h);
    mVelocity = h * 0.8f + up() * v;
}

// A player coming up to him starts a conversation: he barks his next line
// (as a Toad greets Mario); E near him starts the next one once he has
// finished (port_talk_take_press is edge-triggered, so holding E doesn't
// repeat it).  The bark always starts from another animation, so the other
// players' games see it start (updateCopy).
void BarneyNPC::updateTalk(const TVec3f& rNearest) {
    bool pressed = port_talk_take_press() != 0;  // taken every frame: a press far away is not kept for later
    mLastPos = mPosition;
    if (!mStopped) {
        // Everyone left mid-bark: back to running now, not when the bark
        // would have ended (that countdown kept the talk pose on his legs).
        mBalloon->close();
        mTalkFrames = 0;
        mTalked = false;
        return;
    }
    if (!mTalked || (pressed && mTalkFrames == 0 && MR::isNearPlayer(this, cStopFar))) {
        say(mTalked ? "E" : "approached");
        mTalked = true;
        startAnim("talk");
        mTalkFrames = cTalkFrames;
    }
    if (MR::isNearPlayer(this, cHearRadius)) {
        mBalloon->follow(mPosition + up() * cBalloonHeight);
    } else {
        mBalloon->close();
    }
    if (mTalkFrames > 0 && --mTalkFrames == 0) {
        startAnim("wait");
    }
}

void BarneyNPC::playRunSmoke() {
    MultiEmitter* emitter = MR::emitEffect(this, cRunSmokeName);
    if (emitter == nullptr) {
        return;
    }
    TPos3f mtx;
    TVec3f rot;
    MR::makeMtxUpNoSupportPos(&mtx, up(), TVec3f(0.0f, 0.0f, 0.0f));
    MR::makeRTFromMtxPtr(nullptr, &rot, mtx.toMtxPtr(), true);
    emitter->setGlobalScale(cRunSmokeScale, -1);
    emitter->setGlobalRotationDegree(rot, -1);
    emitter->setGlobalTranslation(mPosition, -1);
}

void BarneyNPC::calcAndSetBaseMtx() {
    TVec3f front = mFront;
    MR::vecKillElement(front, up(), &front);
    if (MR::isNearZero(front)) {
        front.set(0.0f, 0.0f, 1.0f);
    }
    MR::normalize(&front);
    TPos3f mtx;
    MR::makeMtxUpFrontPos(&mtx, up(), front, mPosition);
    MR::setBaseTRMtx(this, mtx);
    MR::setBaseScale(this, mScale);
}

// Mario bumps into him instead of through him (NPCActor's push).
// Toads stand in his way: he sidesteps them.
void BarneyNPC::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        MR::sendMsgPush(pReceiver, pSender);
    } else if (MR::isSensorNpc(pReceiver)) {
        TVec3f away = mPosition - pReceiver->mPosition;
        MR::vecKillElement(away, up(), &away);
        if (MR::normalizeOrZero(&away)) {
            away.set(-mFront.x, -mFront.y, -mFront.z);
        }
        mVelocity += away * 2.0f;
    }
}
#endif
