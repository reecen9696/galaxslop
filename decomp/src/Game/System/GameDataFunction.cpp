#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/GameDataGalaxyStorage.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/GameDataTemporaryInGalaxy.hpp"
#include "Game/System/GameEventFlagTable.hpp"
#include "Game/System/GameSequenceDirector.hpp"
#include "Game/System/GameSequenceProgress.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/SaveDataHandleSequence.hpp"
#include "Game/System/ScenarioDataParser.hpp"
#include "Game/System/SysConfigFile.hpp"
#include "Game/System/UserFile.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include <cstdio>
#ifdef TARGET_PC
#include "Game/Util/HashUtil.hpp"
#include "Game/System/GameDataPlayerStatus.hpp"
#include "net/client/progress.h"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "net/client/net_client.h"
#include "port/board.h"
#include "port/heap_routing.h"
#include "port/port.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#endif

namespace {
    SaveDataHandleSequence* getSaveDataHandleSequence() NO_INLINE {
        return SingletonHolder< GameSystem >::get()->mSequenceDirector->mSaveDataHandleSequence;
    }

    GameDataTemporaryInGalaxy* getGameDataTemporaryInGalaxy() {
        return SingletonHolder< GameSystem >::get()->mSequenceDirector->mGameDataTemporaryInGalaxy;
    }

    GameSequenceProgress* getGameSequenceProgress() NO_INLINE {
        return SingletonHolder< GameSystem >::get()->mSequenceDirector->mGameSequenceProgress;
    }
};  // namespace

namespace GameDataFunction {
    bool isDataMario() {
        return getCurrentGameDataHolder()->isDataMario();
    }

    bool hasPowerStar(const char* pGalaxyName, s32 scenarioNo) {
        return getCurrentGameDataHolder()->hasPowerStar(pGalaxyName, scenarioNo);
    }

    bool hasGrandStar(int id) {
        return getCurrentGameDataHolder()->hasGrandStar(id);
    }

    void setGameFlagPowerStarSuccess(const char* pGalaxyName, s32 scenarioNo, bool isOwned) {
        getCurrentGameDataHolder()->setPowerStar(pGalaxyName, scenarioNo, isOwned);
    }

    GameDataSomeScenarioAccessor makeGalaxyScenarioAccessor(const char* pGalaxyName, s32 scenarioNo) {
        return getCurrentGameDataHolder()->makeGalaxyScenarioAccessor(pGalaxyName, scenarioNo);
    }

    bool isOnGalaxyScenarioFlagAlreadyVisited(const char* pGalaxyName, s32 scenarioNo) {
        return getCurrentGameDataHolder()->isOnGalaxyScenarioFlagAlreadyVisited(pGalaxyName, scenarioNo);
    }

    void onGalaxyScenarioFlagAlreadyVisited(const char* pGalaxyName, s32 scenarioNo) {
        getCurrentGameDataHolder()->onGalaxyScenarioFlagAlreadyVisited(pGalaxyName, scenarioNo);
    }

    bool isAppearGalaxy(const char* pGalaxyName) {
        return getCurrentGameDataHolder()->isAppearGalaxy(pGalaxyName);
    }

    s32 calcCurrentPowerStarNum() {
        return getCurrentGameDataHolder()->calcCurrentPowerStarNum();
    }

    s32 getPowerStarNumOwned(const char* pGalaxyName) {
        return getCurrentGameDataHolder()->getPowerStarNumOwned(pGalaxyName);
    }

    s32 getPowerStarNumMax() {
        s32 powerStarNumMax = 0;

        for (ScenarioDataIter iter = MR::makeBeginScenarioDataIter(); !iter.isEnd(); iter.goNext()) {
            powerStarNumMax += iter.makeAccessor().getPowerStarNum();
        }

        return powerStarNumMax;
    }

    s32 getPictureBookChapterCanRead() {
        return getCurrentGameDataHolder()->getPictureBookChapterCanRead();
    }

    u32 getPictureBookChapterAlreadyRead() {
        return getCurrentGameDataHolder()->getPictureBookChapterAlreadyRead();
    }

    void setPictureBookChapterAlreadyRead(int chapterAlreadyRead) {
        getCurrentGameDataHolder()->setPictureBookChapterAlreadyRead(chapterAlreadyRead);
    }

    void onGameEventFlag(const char* pName) {
        getCurrentGameDataHolder()->tryOnGameEventFlag(pName);
    }

    bool canOnGameEventFlag(const char* pName) {
        return getCurrentGameDataHolder()->canOnGameEventFlag(pName);
    }

    bool canOnAndIsOffGameEventFlag(const char* pName) {
        return getCurrentGameDataHolder()->canOnGameEventFlag(pName) && !getCurrentGameDataHolder()->isOnGameEventFlag(pName);
    }

    bool isOnGameEventFlag(const char* pName) {
        return getCurrentGameDataHolder()->isOnGameEventFlag(pName);
    }

    u32 getGameEventValue(const char* pName) {
        return getCurrentGameDataHolder()->getGameEventValue(pName);
    }

    void setGameEventValue(const char* pName, u16 value) {
        getCurrentGameDataHolder()->setGameEventValue(pName, value);
    }

    bool isOnGameEventValueForBit(const char* pName, int index) {
        return getCurrentGameDataHolder()->isOnGameEventValueForBit(pName, index);
    }

    void setGameEventValueForBit(const char* pName, int index, bool isOn) {
        getCurrentGameDataHolder()->setGameEventValueForBit(pName, index, isOn);
    }

    s32 setupAlreadyDoneFlag(const char* pZoneName, const JMapInfoIter& rIter, u32* pParam3) {
        return ::getGameDataTemporaryInGalaxy()->setupAlreadyDoneFlag(pZoneName, rIter, pParam3);
    }

    void updateAlreadyDoneFlag(int param1, u32 param2) {
        ::getGameDataTemporaryInGalaxy()->updateAlreadyDoneFlag(param1, param2);
    }

    void restoreGalaxyCometStatus(int cometId, u16* pCometStatus, u16* pPastSecond) {
        char eventValueName[20];

        snprintf(eventValueName, sizeof(eventValueName), "Comet%1dStatus", cometId);

        *pCometStatus = getCurrentGameDataHolder()->getGameEventValue(eventValueName);
    }

    void setRaceBestTime(const char* pRaceName, u32 bestTime) {
        getCurrentGameDataHolder()->setRaceBestTime(pRaceName, bestTime);
    }

    s32 getRaceBestTime(const char* pRaceName) {
        return getCurrentGameDataHolder()->getRaceBestTime(pRaceName);
    }

    void addMissPoint(int num) {
        getCurrentGameDataHolder()->addMissPoint(num);
    }

    void resetMissPoint() {
        getCurrentGameDataHolder()->resetMissPoint();
    }

    bool isPointCollectForLetter() {
        return getCurrentGameDataHolder()->isPointCollectForLetter();
    }

    void incPlayerMissNum() {
        getCurrentGameDataHolder()->incPlayerMissNum();
    }

    s32 registerStorageSpinDriverPathDrawRange(const char* pGalaxyName, int scenarioNo, int zoneId, int param4, f32* pParam5) {
        return getCurrentGameDataHolder()->setupSpinDriverPathStorage(pGalaxyName, scenarioNo, zoneId, param4, pParam5);
    }

    void updateStorageSpinDriverPathDrawRange(const char* pGalaxyName, int scenarioNo, int param3, f32 param4) {
        getCurrentGameDataHolder()->updateSpinDriverPathStorage(pGalaxyName, scenarioNo, param3, param4);
    }

    u32 getStarPieceNumGivingToTicoSeed(int index) {
        return getCurrentGameDataHolder()->getStarPieceNumGivingToTicoSeed(index);
    }

    u32 getStarPieceNumMaxGivingToTicoSeed(int index) {
        return getCurrentGameDataHolder()->getStarPieceNumMaxGivingToTicoSeed(index);
    }

    void addStarPieceGivingToTicoSeed(int index, int num) {
        getCurrentGameDataHolder()->addStarPieceGivingToTicoSeed(index, num);
    }

    bool isPassedStoryEvent(const char* pStoryEventName) {
        return getCurrentGameDataHolder()->isPassedStoryEvent(pStoryEventName);
    }

    void followStoryEventByName(const char* pStoryEventName) {
        if (getCurrentGameDataHolder()->isPassedStoryEvent(pStoryEventName)) {
            return;
        }

        getCurrentGameDataHolder()->followStoryEventByName(pStoryEventName);
    }

    s32 getPlayerLeft() {
        return getCurrentGameDataHolder()->getPlayerLeft();
    }

    void addPlayerLeft(int num) {
        getCurrentGameDataHolder()->addPlayerLeft(num);
    }

    bool isPlayerLeftSupply() {
        return getCurrentGameDataHolder()->isPlayerLeftSupply();
    }

    void offPlayerLeftSupply() {
        getCurrentGameDataHolder()->offPlayerLeftSupply();
    }

    s32 getStockedStarPieceNum() {
        return getCurrentGameDataHolder()->getStockedStarPieceNum();
    }

    s32 getStarPieceNum() {
        return ::getGameDataTemporaryInGalaxy()->getStarPieceNum();
    }

    void addStarPiece(int num) {
        ::getGameDataTemporaryInGalaxy()->addStarPiece(num);
    }

    void setLast1upStarPieceNum(int last1upStarPieceNum) {
        ::getGameDataTemporaryInGalaxy()->setLast1upStarPieceNum(last1upStarPieceNum);
    }

    s32 getLast1upStarPieceNum() {
        return ::getGameDataTemporaryInGalaxy()->mLast1upStarPieceNum;
    }

    const wchar_t* getUserName() {
        return ::getSaveDataHandleSequence()->getCurrentUserFile()->mUserName;
    }

    u16 getUserFileIndex() {
        const char* pGameDataName = ::getSaveDataHandleSequence()->getCurrentUserFile()->getGameDataName();
        s32 index = strlen(pGameDataName) - 1;
        char ch = pGameDataName[index];

        if (ch <= '0' || ch > '9') {
            return 1;
        }

        return ch - '0';
    }

    void resetAllGameData() {
        getCurrentGameDataHolder()->resetAllData();
    }

    bool isOnJustGameEventFlag(const char* pName) {
        return getCurrentGameDataHolder()->isOnGameEventFlag(pName) && !getSceneStartGameDataHolder()->isOnGameEventFlag(pName);
    }

    bool canOnJustGameEventFlag(const char* pName) {
        return getCurrentGameDataHolder()->canOnGameEventFlag(pName) && !getSceneStartGameDataHolder()->canOnGameEventFlag(pName);
    }

    bool isOnJustAppearGalaxy(const char* pGalaxyName) {
        return getCurrentGameDataHolder()->isAppearGalaxy(pGalaxyName) && !getSceneStartGameDataHolder()->isAppearGalaxy(pGalaxyName);
    }

    bool isEqualJustPowerStarNum(int powerStarNum) {
        return getCurrentGameDataHolder()->calcCurrentPowerStarNum() == powerStarNum &&
               getSceneStartGameDataHolder()->calcCurrentPowerStarNum() != powerStarNum;
    }

    GameDataHolder* getCurrentGameDataHolder() {
        return ::getSaveDataHandleSequence()->getCurrentUserFile()->mGameDataHolder;
    }

    GameDataHolder* getSceneStartGameDataHolder() {
        return ::getSaveDataHandleSequence()->getBackupUserFile()->mGameDataHolder;
    }

    s32 calcTicoGalaxyNum(const GameDataHolder* pGameDataHolder) {
        s32 ticoGalaxyNum = 0;
        s32 exclamationGalaxyNum = GameEventFlagTable::calcExclamationGalaxyNum();

        for (s32 i = 0; i < exclamationGalaxyNum; i++) {
            const char* pExclamationGalaxyName = GameEventFlagTable::getExclamationGalaxyNameFromIndex(i);

            if (pGameDataHolder->isAppearGalaxy(pExclamationGalaxyName)) {
                ticoGalaxyNum++;
            }
        }

        return ticoGalaxyNum;
    }

    s32 calcGrandStarNum(const GameDataHolder* pGameDataHolder) {
        s32 grandStarNum = 0;

        for (int i = 1; i <= 7; i++) {
            if (pGameDataHolder->hasGrandStar(i)) {
                grandStarNum++;
            }
        }

        return grandStarNum;
    }

    void resetGameDataGoToGalaxyFirst() {
        ::getGameDataTemporaryInGalaxy()->resetRaceBestTime();
        ::getGameDataTemporaryInGalaxy()->resetPlayerRestartIdInfo();
        ::getGameDataTemporaryInGalaxy()->_4 = false;
        ::getGameDataTemporaryInGalaxy()->clearAlreadyDoneFlag();
        ::getGameDataTemporaryInGalaxy()->resetStageResultStarPieceParam();
        ::getGameDataTemporaryInGalaxy()->resetStageResultCoinParam();
    }

    void resetGameDataGoToGalaxyRetry() {
        ::getGameDataTemporaryInGalaxy()->resetRaceBestTime();
        ::getGameDataTemporaryInGalaxy()->_4 = true;
    }

    void onCompleteEndingCurrentPlayer() {
        ::getSaveDataHandleSequence()->getCurrentUserFile()->onCompleteEndingCurrentPlayer();
    }

    bool isOnCompleteEndingMarioAndLuigi() {
        return ::getSaveDataHandleSequence()->getCurrentUserFile()->isOnCompleteEndingMarioAndLuigi();
    }

    OSTime getSysConfigFileTimeAnnounced() {
        return ::getSaveDataHandleSequence()->getSysConfigFile()->getTimeAnnounced();
    }

    void updateSysConfigFileTimeAnnounced() {
        ::getSaveDataHandleSequence()->getSysConfigFile()->updateTimeAnnounced();
    }

    OSTime getSysConfigFileTimeSent() {
        return ::getSaveDataHandleSequence()->getSysConfigFile()->getTimeSent();
    }

    void setSysConfigFileTimeSent(OSTime timeSent) {
        ::getSaveDataHandleSequence()->getSysConfigFile()->setTimeSent(timeSent);
    }

    u32 getSysConfigFileSentBytes() {
        return ::getSaveDataHandleSequence()->getSysConfigFile()->getSentBytes();
    }

    void setSysConfigFileSentBytes(u32 sentBytes) {
        ::getSaveDataHandleSequence()->getSysConfigFile()->setSentBytes(sentBytes);
    }

    bool isLuigiLeftSupply() {
        return ::getGameSequenceProgress()->isLuigiLeftSupply();
    }

    void offLuigiLeftSupply() {
        ::getGameSequenceProgress()->offLuigiLeftSupply();
    }

    bool isOnGameEventFlagNormalComet(const char* pGalaxyName) {
        char eventFlagName[64];

        snprintf(eventFlagName, sizeof(eventFlagName), "NormalComet%s", pGalaxyName);

        return getCurrentGameDataHolder()->isOnGameEventFlag(eventFlagName);
    }

    bool isOnGameEventFlagCoin100Comet(const char* pGalaxyName) {
        char eventFlagName[64];

        snprintf(eventFlagName, sizeof(eventFlagName), "Coin100Comet%s", pGalaxyName);

        return getCurrentGameDataHolder()->isOnGameEventFlag(eventFlagName);
    }

    void updateGalaxyCometStatus(int cometId, u16 cometStatus, u16 pastSecond) {
        char eventValueName[20];

        snprintf(eventValueName, sizeof(eventValueName), "Comet%1dStatus", cometId);

        getCurrentGameDataHolder()->setGameEventValue(eventValueName, cometStatus);
    }

    void addStockedStarPiece(int num) {
        getCurrentGameDataHolder()->addStockedStarPiece(num);

        if (getCurrentGameDataHolder()->getStockedStarPieceNum() != 9999) {
            return;
        }

        const char* pEventFlagName = "StarPieceCounterStop";

        if (getCurrentGameDataHolder()->isOnGameEventFlag(pEventFlagName)) {
            return;
        }

        getCurrentGameDataHolder()->tryOnGameEventFlag(pEventFlagName);
    }

    bool isPowerStarLeftInCometOnly() {
        const char* pGalaxyName;

        for (ScenarioDataIter iter = MR::makeBeginScenarioDataIter(); !iter.isEnd(); iter.goNext()) {
            GalaxyStatusAccessor accessor = iter.makeAccessor();

            if (accessor.getPowerStarNum() == 0) {
                continue;
            }

            for (int i = 1; i <= accessor.getPowerStarNum(); i++) {
                if (accessor.isCometStar(i)) {
                    continue;
                }

                pGalaxyName = accessor.getName();

                if (getCurrentGameDataHolder()->hasPowerStar(pGalaxyName, i)) {
                    continue;
                }

                if (GameEventFlagTable::isPowerStarType(accessor.getName(), i, "SpecialStarFinalChallenge")) {
                    continue;
                }

                return false;
            }
        }

        return true;
    }

    s32 calcGreenStarNum(const GameDataHolder* pGameDataHolder) {
        s32 greenStarNum = 0;
        char eventFlagName[32];

        for (int i = 1; i <= 3; i++) {
            snprintf(eventFlagName, sizeof(eventFlagName), "SpecialStarGreen%1d", i);

            if (pGameDataHolder->isOnGameEventFlag(eventFlagName)) {
                greenStarNum++;
            }
        }

        return greenStarNum;
    }
};  // namespace GameDataFunction

#ifdef TARGET_PC
// The room takes its host's progress (P3-07).  Every client offers its
// current file, in the save's own format, to the relay, which passes the
// host's (the room's owner) to everyone else.  A save gets what it lacks
// from the host's, as with the log; a brand-new file started in the room
// becomes a copy of it (port_net_dropin_new_file).
namespace {
    u8 sSnapBuf[PORT_NET_SNAPSHOT_MAX];

    // The host's progress in sSnapBuf; 0 when there is none.
    u32 takeHostProgress() {
        return port_net_host_snapshot(sSnapBuf, sizeof(sSnapBuf), nullptr, nullptr);
    }

    // The host's progress, read into a holder of its own.  It lives in host
    // memory: the game's heaps have no room to spare for it (the stationed
    // heap is full, and the scene heap goes away with the scene).
    GameDataHolder* readHostProgress() {
        u32 version = 0;
        u8 origin = 0;
        u32 len = port_net_host_snapshot(sSnapBuf, sizeof(sSnapBuf), &version, &origin);
        if (len == 0) {
            return nullptr;
        }
        static GameDataHolder* sHost = nullptr;
        if (sHost == nullptr) {
            GameDataHolder* current = GameDataFunction::getCurrentGameDataHolder();
            PortHostAllocScope hostAlloc;
            sHost = new GameDataHolder(current != nullptr ? current->mUserFile : nullptr);
        }
        sHost->resetAllData();
        if (!sHost->loadFromFileBinary("host", sSnapBuf, len)) {
            port_log("net: host progress from player %u unreadable (%u bytes)", origin, len);
            return nullptr;
        }
        return sHost;
    }

    bool isSharedFlag(const GameEventFlag* pFlag) {
        if (pFlag == nullptr || (pFlag->mSaveFlag & 0x1) != 0) {
            return false;  // computed from stars and other flags
        }
        switch (pFlag->mType) {
        case GameEventFlag::Type_SpecialStar:
        case GameEventFlag::Type_EventFlag:
        case GameEventFlag::Type_StoryEvent:
        case GameEventFlag::Type_Galaxy:
            return true;
        default:
            return false;  // tutorials stay per player, as in the log
        }
    }

    // Gives `pLocal` the stars, shared flags and story progress the host has
    // and it lacks.  Never takes anything away.
    void mergeHostProgress(GameDataHolder* pLocal, GameDataHolder* pHost) {
        port_net_progress_applying(1);
        s32 stars = 0, flags = 0;
        GameDataAllGalaxyStorage* all = pHost->mAllGalaxyStorage;
        for (s32 i = 0; i < all->getGalaxyNum(); i++) {
            const char* galaxy = all->getGalaxyStorage(i)->mGalaxyName;
            for (s32 scenario = 1; scenario <= 8; scenario++) {
                if (pHost->hasPowerStar(galaxy, scenario) && !pLocal->hasPowerStar(galaxy, scenario)) {
                    pLocal->setPowerStar(galaxy, scenario, true);
                    stars++;
                }
            }
        }
        for (s32 i = 0; i < GameEventFlagTable::getTableSize(); i++) {
            const GameEventFlag* flag = GameEventFlagTable::getFlag(i);
            if (isSharedFlag(flag) && pHost->isOnGameEventFlag(flag->mName) && !pLocal->isOnGameEventFlag(flag->mName)) {
                pLocal->tryOnGameEventFlag(flag->mName);
                flags++;
            }
        }
        u8 story = pHost->mPlayerStatus->mStoryProgress;
        bool storyUp = story > pLocal->mPlayerStatus->mStoryProgress;
        if (storyUp) {
            pLocal->mPlayerStatus->mStoryProgress = story;
        }
        port_net_progress_applying(0);
        if (stars != 0 || flags != 0 || storyUp) {
            port_log("net: host progress: %d stars, %d flags%s", stars, flags, storyUp ? ", story" : "");
        }
    }

    // Each gameplay frame: offer this client's progress now and then, and
    // merge the host's when a new one arrives.
    void netHostProgress(GameDataHolder* pHolder) {
        if (pHolder == nullptr || !port_net_in_room()) {
            return;
        }
        static s32 sFrames = 0;
        if (sFrames++ % 120 == 0) {
            u32 len = pHolder->makeFileBinary(sSnapBuf, sizeof(sSnapBuf));
            if (len > 0 && len <= sizeof(sSnapBuf)) {
                port_net_snapshot_offer(sSnapBuf, len);
            }
        }
        static u32 sMerged = 0;
        u32 version = 0;
        port_net_host_snapshot(nullptr, 0, &version, nullptr);
        if (version == sMerged) {
            return;
        }
        sMerged = version;
        if (GameDataHolder* host = readHostProgress()) {
            mergeHostProgress(pHolder, host);
        }
    }
}  // namespace

// StorySequenceExecutor::overwriteGalaxyNameAfterLoading, as a file starts:
// a brand-new file (no story yet, no stars) started in a room becomes a copy
// of the host's progress, so the game sends it where the host's save would
// go (the Observatory) instead of the prologue.  SMG_DROP_IN=0 turns it off.
extern "C" void port_net_dropin_new_file() {
    const char* setting = getenv("SMG_DROP_IN");
    if (!port_net_in_room() || (setting != nullptr && strcmp(setting, "0") == 0)) {
        return;
    }
    GameDataHolder* local = GameDataFunction::getCurrentGameDataHolder();
    if (local == nullptr || local->mPlayerStatus->mStoryProgress != 0 || local->calcCurrentPowerStarNum() != 0) {
        return;  // not a new file: it gets what it lacks during play instead
    }
    u32 len = takeHostProgress();
    if (len == 0) {
        port_log("net: new file, no host progress yet: the prologue as usual");
        return;
    }
    // The copy goes into the file and into its scene-start copy, which the
    // game compares against for "just got" moments and saves from early on.
    GameDataHolder* files[2] = {local, GameDataFunction::getSceneStartGameDataHolder()};
    for (GameDataHolder* file : files) {
        if (file == nullptr) {
            continue;
        }
        char name[sizeof(file->mName)];
        snprintf(name, sizeof(name), "%s", file->mName);
        file->resetAllData();
        if (!file->loadFromFileBinary(name, sSnapBuf, len)) {
            file->resetAllData();  // unreadable: a new file after all
            port_log("net: host progress unreadable, the prologue as usual");
            return;
        }
    }
    port_log("net: new file starts with the host's progress (%d stars)", local->calcCurrentPowerStarNum());
}

#endif

#ifdef TARGET_PC
// Co-op (P3-02, D-022): applies what the room's progress log holds that this
// save lacks.  Never clears anything.  Called from GameScene during gameplay.
extern "C" void port_net_apply_progress() {
    // Debug: PETARI_FAKESTAR=Galaxy:scenario marks a star as got through the
    // game's own path once gameplay starts (tests the sync without playing).
    static bool sFaked = false;
    if (!sFaked) {
        sFaked = true;
        // PETARI_FAKE_UNSHARED=1: the fakes stay out of the room's log, like
        // progress made before the room (tests the host's progress, P3-07).
        const char* unshared = getenv("PETARI_FAKE_UNSHARED");
        port_net_progress_applying(unshared != nullptr && *unshared == '1');
        if (const char* fake = getenv("PETARI_FAKESTAR")) {  // Galaxy:scenario[,Galaxy:scenario...]
            char list[256];
            snprintf(list, sizeof(list), "%s", fake);
            for (char* galaxy = strtok(list, ","); galaxy != nullptr; galaxy = strtok(nullptr, ",")) {
                int scenario = 1;
                if (char* colon = strchr(galaxy, ':')) {
                    *colon = 0;
                    scenario = atoi(colon + 1);
                }
                GameDataFunction::setGameFlagPowerStarSuccess(galaxy, scenario, true);
                port_log("net: PETARI_FAKESTAR %s %d", galaxy, scenario);
            }
        }
        // PETARI_FAKEFLAG=Name[,Name...] turns event flags on the same way.
        if (const char* fake = getenv("PETARI_FAKEFLAG")) {
            char names[256];
            snprintf(names, sizeof(names), "%s", fake);
            for (char* name = strtok(names, ","); name != nullptr; name = strtok(nullptr, ",")) {
                GameDataFunction::getCurrentGameDataHolder()->tryOnGameEventFlag(name);
                port_log("net: PETARI_FAKEFLAG %s", name);
            }
        }
        port_net_progress_applying(0);
    }
    PortNetProgress p;
    while (port_net_progress_poll(&p)) {
        GameDataHolder* holder = GameDataFunction::getCurrentGameDataHolder();
        if (holder == nullptr) {
            continue;
        }
        port_net_progress_applying(1);
        if (p.kind == net::kProgressStar) {
            GameDataAllGalaxyStorage* all = holder->mAllGalaxyStorage;
            for (s32 i = 0; i < all->getGalaxyNum(); i++) {
                const char* galaxy = all->getGalaxyStorage(i)->mGalaxyName;
                if (MR::getHashCode(galaxy) != p.stageHash) {
                    continue;
                }
                if (!holder->hasPowerStar(galaxy, p.scenario)) {
                    holder->setPowerStar(galaxy, p.scenario, true);
                    port_log("net: power star %s %u from %s", galaxy, p.scenario, port_net_player_name(p.origin));
                }
                break;
            }
        } else if (p.kind == net::kProgressStoryFlag) {
            const GameEventFlag* flag = p.arg < GameEventFlagTable::getTableSize() ? GameEventFlagTable::getFlag(p.arg) : nullptr;
            if (flag != nullptr && !holder->isOnGameEventFlag(flag->mName)) {
                holder->tryOnGameEventFlag(flag->mName);
                port_log("net: flag %s from %s", flag->mName, port_net_player_name(p.origin));
            }
        } else if (p.kind == net::kProgressGrandStar) {
            char flag[32];
            snprintf(flag, sizeof(flag), "SpecialStarGrand%u", p.arg);
            if (!holder->isOnGameEventFlag(flag)) {
                holder->tryOnGameEventFlag(flag);
                port_log("net: grand star %u from %s", p.arg, port_net_player_name(p.origin));
            }
        }
        port_net_progress_applying(0);
    }
    netHostProgress(GameDataFunction::getCurrentGameDataHolder());
}
#endif

#ifdef TARGET_PC
// Follow (P3-04): the page (or a key) asks to go where a remote player is.
// Players carry MR::getHashCode(stage name); the galaxy list of the loaded
// save turns that back into a name.  The table is built on the game thread
// and read by the browser presenter for the player list.
namespace {
    const s32 kMaxStages = 128;
    u32 sStageHashes[kMaxStages];
    const char* sStageNames[kMaxStages];
    std::atomic<s32> sStageCount{0};
    std::atomic<s32> sFollowSlot{-1};

    void buildStageTable(GameDataHolder* holder) {
        if (sStageCount.load() > 0 || holder == nullptr) {
            return;
        }
        GameDataAllGalaxyStorage* all = holder->mAllGalaxyStorage;
        s32 n = 0;
        for (s32 i = 0; i < all->getGalaxyNum() && n < kMaxStages; i++) {
            const char* name = all->getGalaxyStorage(i)->mGalaxyName;
            sStageNames[n] = name;
            sStageHashes[n] = MR::getHashCode(name);
            n++;
        }
        sStageCount.store(n);
    }
}  // namespace

extern "C" const char* port_net_stage_name(u32 hash) {
    s32 n = sStageCount.load();
    for (s32 i = 0; i < n; i++) {
        if (sStageHashes[i] == hash) return sStageNames[i];
    }
    return nullptr;
}

extern "C" void port_net_request_follow(int slot) { sFollowSlot.store(slot); }

// GameScene, during gameplay.
extern "C" void port_net_follow_update() {
    buildStageTable(GameDataFunction::getCurrentGameDataHolder());
    static u32 sLastStage = 0;
    static s32 sLastScenario = -1;
    u32 here = MR::getHashCode(MR::getCurrentStageName());
    if (here != sLastStage || MR::getCurrentScenarioNo() != sLastScenario) {
        sLastStage = here;
        sLastScenario = MR::getCurrentScenarioNo();
        port_log("net: now in %s %d", MR::getCurrentStageName(), (int)sLastScenario);
    }
    port_board_stage(MR::getCurrentStageName(), MR::getCurrentScenarioNo());  // leaderboard runs (P5-07)
    s32 slot = sFollowSlot.exchange(-1);
    if (slot < 0 || slot >= net::kMaxRemotes) {
        return;
    }
    const net::RemoteView& v = net::netRemote(slot);
    const char* stage = v.present ? port_net_stage_name(v.pose.stageHash) : nullptr;
    if (stage == nullptr) {
        port_log("net: cannot follow slot %d (no galaxy known)", slot);
        return;
    }
    if (v.pose.stageHash == MR::getHashCode(MR::getCurrentStageName()) && v.pose.scenario == MR::getCurrentScenarioNo()) {
        return;  // already there
    }
    port_log("net: following %s to %s %d", v.name, stage, (int)v.pose.scenario);
    MR::requestChangeStageInGameMoving(stage, v.pose.scenario);
}
#endif
