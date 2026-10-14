#ifdef TARGET_PC
#include "net/world/world_sync.h"
#include "port/port.h"
#endif
#include "Game/MapObj/ChipGroup.hpp"
#include "Game/Map/StageSwitch.hpp"
#include "Game/MapObj/ChipBase.hpp"
#include "Game/MapObj/ChipHolder.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/MapObj/ChipCounter.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/JMapUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

ChipGroup::ChipGroup(const char* pName, s32 chipType) : NameObj(pName) {
    ChipEntry* pEntry = mChips;

    do {
        pEntry->mChip = nullptr;
        pEntry->mIsGotten = false;
        pEntry++;
    } while (pEntry < mChips + 5);

    mSwitchCtrl = nullptr;
    mGotCount = 0;
    mTotalCount = 0;
    mStageSwitchArg = -1;
    mChipType = chipType;
    mFlashFrame = -1;
    mCounterTimer = -1;
    mUIRange = -1.0f;
    mUIRangeFixed = -1.0f;
    mIsDeactivated = 0;
    mAlreadyDoneFlag = -1;
    mIsListeningAppear = false;
    mIsCounterVisible = false;
}

void ChipGroup::updateUIRange() {
    if (mUIRangeFixed < 0.0f) {
        TBox3f range;

        for (s32 i = 0; i < mTotalCount; i++) {
            TBox3f currBound;
            TVec3f pos = mChips[i].mChip->mPosition;

            TVec3f center = pos;
            TVec3f pad(2000.0f);
            currBound.set(center - pad, center + pad);

            if (i == 0) {
                range.i.set(currBound.i);
                range.f.set(currBound.f);
            } else {
                range.extend(currBound.i, currBound.f);
            }
        }

        range.getCenter(&mUIPos);

        TVec3f diff;
        diff.sub(range.f, range.i);
        mUIRange = diff.length() / 2.0f;
    } else {
        mUIRange = mUIRangeFixed;
    }
}

void ChipGroup::movement() {
    if (!mIsDeactivated) {
        if (mIsCounterVisible) {
            if (mUIPos.distance(*MR::getPlayerPos()) < mUIRange) {
                MR::showChipCounter(mChipType, mStageSwitchArg);
            } else {
                MR::hideChipCounter(mChipType, mStageSwitchArg);
            }
        }

        if (mCounterTimer >= 0) {
            mCounterTimer--;

            if (mCounterTimer < 0) {
                MR::hideChipCounter(mChipType, mStageSwitchArg);
                mCounterTimer = -1;
                mIsCounterVisible = false;
            }
        }
    }
}

void ChipGroup::registerChip(ChipBase* pChip) {
    if (mIsListeningAppear) {
        pChip->makeActorDead();
    }

    if (mIsDeactivated) {
        pChip->deactive();
    }

    mChips[mTotalCount].mChip = pChip;
    mTotalCount++;
    updateUIRange();
}

void ChipGroup::noticeEndCompleteDemo() {
    mSwitchCtrl->onSwitchA();
}

s32 ChipGroup::getGotCount() const {
    return mGotCount;
}

bool ChipGroup::isComplete() const {
    for (s32 i = 0; i < mTotalCount; i++) {
        if (!mChips[i].mIsGotten) {
            return false;
        }
    }

    return true;
}

BlueChipGroup::BlueChipGroup(const char* pName) : ChipGroup(pName, ChipBase::Type_Blue) {
}

ChipGroup::~ChipGroup() {
}

YellowChipGroup::YellowChipGroup(const char* pName) : ChipGroup(pName, ChipBase::Type_Yellow) {
}

void ChipGroup::init(const JMapInfoIter& rIter) {
    MR::getJMapInfoTrans(rIter, &mUIPos);
    MR::getJMapInfoArg0WithInit(rIter, &mStageSwitchArg);
    MR::getJMapInfoArg1WithInit(rIter, &mFlashFrame);
    mUIRangeFixed = -1.0f;
    MR::getJMapInfoArg2NoInit(rIter, &mUIRangeFixed);

    s32 arg3 = 0;
    MR::getJMapInfoArg3NoInit(rIter, &arg3);

    if (arg3 == 1) {
        mIsDeactivated = 0;
        mAlreadyDoneFlag = MR::setupAlreadyDoneFlag("\x83\x60\x83\x62\x83\x76\x8f\x57\x82\xdf\x8d\xcf\x82\xdd", rIter, &mIsDeactivated);
    } else {
        mIsDeactivated = 0;
        mAlreadyDoneFlag = -1;
    }

    MR::createChipHolder(mChipType);
    MR::registerChipGroup(mChipType, this);
    mSwitchCtrl = MR::createStageSwitchCtrl(this, rIter);

    if (mSwitchCtrl->isValidSwitchAppear()) {
        MR::listenNameObjStageSwitchOnAppear(this, mSwitchCtrl, MR::Functor(this, &ChipGroup::receiveAppearRequest));
        mIsListeningAppear = true;
    } else {
        mIsListeningAppear = false;
    }

    MR::connectToSceneMapObjMovement(this);
}

void ChipGroup::noticeGet(ChipBase* pChip) {
    if (mIsDeactivated) {
        return;
    }

    MR::showChipCounter(mChipType, mStageSwitchArg);

    if (!mIsListeningAppear || mFlashFrame == -1) {
        mIsCounterVisible = true;
    }

    for (s32 i = 0; i < mTotalCount; i++) {
        bool isMatch = false;

        if (pChip && mChips[i].mChip == pChip) {
            isMatch = true;
        }

        if (isMatch) {
            mChips[i].mIsGotten = true;
            mGotCount++;
#ifdef TARGET_PC
            // Co-op: gone for the others too, and counted there (one shared
            // set of chips per group).
            port_world_chip_got(mChipType, mStageSwitchArg, i);
#endif

            if (isComplete()) {
                MR::requestStartChipCompleteDemo(mChipType, mStageSwitchArg);
                MR::requestMovementOn(pChip);

                if (mAlreadyDoneFlag >= 0) {
                    MR::updateAlreadyDoneFlag(mAlreadyDoneFlag, 1);
                }

                if (mChipType == 0) {
                    MR::startSystemSE("SE_OJ_BLUECHIP_COMPLETE", mGotCount, -1);
                } else if (mChipType == 1) {
                    MR::startSystemSE("SE_OJ_YELLOWCHIP_COMPLETE", mGotCount, -1);
                }

                mCounterTimer = -1;
                mIsCounterVisible = false;
                return;
            }

            return;
        }
    }
}

void ChipGroup::receiveAppearRequest() {
    if (mIsDeactivated) {
        return;
    }

    for (s32 i = 0; i < mTotalCount; i++) {
        if (mFlashFrame >= 0) {
            mChips[i].mChip->appearFlashing(mFlashFrame);
        } else {
            mChips[i].mChip->appearWait();
        }
    }

    MR::showChipCounter(mChipType, mStageSwitchArg);

    if (mFlashFrame >= 0) {
        mCounterTimer = mFlashFrame;
        mIsCounterVisible = false;
    } else {
        mCounterTimer = -1;
        mIsCounterVisible = true;
    }

    if (mChipType == 0) {
        MR::startSystemSE("SE_OJ_BLUECHIP_APPEAR");
    } else if (mChipType == 1) {
        MR::startSystemSE("SE_OJ_YELLOWCHIP_APPEAR");
    }
}

BlueChipGroup::~BlueChipGroup() {
}

YellowChipGroup::~YellowChipGroup() {
}

#ifdef TARGET_PC
// net/world: another player took chip `index` of group `groupId`.  It goes
// here too and counts, with the pickup sound but no completion demo (the
// taker's demo turns the group's switch on, and switch sync brings that).
// A chip not out yet (in a shell, waiting for its switch) never comes out.
// Returns whether it counted (0: already taken here, or no such chip).
extern "C" int port_chip_take_remote(int chipType, int groupId, int index) {
    if (chipType != ChipBase::Type_Blue && chipType != ChipBase::Type_Yellow) {
        return 0;
    }
    if (!MR::isExistSceneObj(chipType == ChipBase::Type_Blue ? SceneObj_BlueChipHolder : SceneObj_YellowChipHolder)) {
        return 0;
    }
    ChipHolder* holder = MR::getChipHolder(chipType);
    ChipGroup* group = holder->findChipGroup(groupId);
    if (group == nullptr || group->mIsDeactivated || index < 0 || index >= group->mTotalCount || group->mChips[index].mIsGotten) {
        return 0;
    }
    ChipBase* chip = group->mChips[index].mChip;
    group->mChips[index].mIsGotten = true;
    group->mGotCount++;
    holder->mChipCounter->setCount(group->mGotCount);
    MR::showChipCounter(chipType, groupId);
    if (MR::isDead(chip)) {
        chip->deactive();
    } else {
        if (chip->hasAirBubble()) {
            chip->mAirBubble->kill();
        }
        MR::emitEffect(chip, "Get");
        chip->kill();
    }
    if (group->isComplete()) {
        if (group->mAlreadyDoneFlag >= 0) {
            MR::updateAlreadyDoneFlag(group->mAlreadyDoneFlag, 1);
        }
        MR::startSystemSE(chipType == ChipBase::Type_Blue ? "SE_OJ_BLUECHIP_COMPLETE" : "SE_OJ_YELLOWCHIP_COMPLETE", group->mGotCount, -1);
        group->mIsCounterVisible = false;
        group->mCounterTimer = 120;  // the full counter for 2 s, then hidden
    } else {
        MR::startSystemSE(chipType == ChipBase::Type_Blue ? "SE_SY_BLUECHIP_GET" : "SE_SY_YELLOWCHIP_GET", group->mGotCount, -1);
        if (!group->mIsListeningAppear || group->mFlashFrame == -1) {
            group->mIsCounterVisible = true;
        }
    }
    return 1;
}

// Debug (mode 106): the local player takes the nearest chip not taken yet,
// as touching it does; one not out yet (dead, waiting for its switch) is
// counted the same way.  0 if none.
extern "C" int port_chip_debug_take_nearest(float x, float y, float z) {
    ChipBase* best = nullptr;
    f32 bestD = 1e30f;
    s32 chips = 0, gotten = 0, out = 0;
    for (s32 type = ChipBase::Type_Blue; type <= ChipBase::Type_Yellow; type++) {
        if (!MR::isExistSceneObj(type == ChipBase::Type_Blue ? SceneObj_BlueChipHolder : SceneObj_YellowChipHolder)) {
            continue;
        }
        ChipHolder* holder = MR::getChipHolder(type);
        for (s32 g = 0; g < holder->mNumChipGroups; g++) {
            ChipGroup* group = holder->mChipGroups[g];
            for (s32 i = 0; i < group->mTotalCount; i++) {
                ChipBase* chip = group->mChips[i].mChip;
                chips++;
                gotten += group->mChips[i].mIsGotten ? 1 : 0;
                out += MR::isDead(chip) ? 0 : 1;
                if (group->mChips[i].mIsGotten) {
                    continue;
                }
                f32 dx = chip->mPosition.x - x, dy = chip->mPosition.y - y, dz = chip->mPosition.z - z;
                f32 d = dx * dx + dy * dy + dz * dz;
                if (d < bestD) {
                    bestD = d;
                    best = chip;
                }
            }
        }
    }
    port_log("chips: %d in the stage, %d out, %d taken", chips, out, gotten);
    if (best == nullptr) {
        return 0;
    }
    if (!best->requestGet(nullptr, nullptr)) {
        MR::noticeGetChip(best->mChipType, best, best->mGroupID);
        best->deactive();
    }
    return 1;
}
#endif
