#include "Game/Scene/ScenePlayingResult.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#ifdef TARGET_PC
#include "port/board.h"
#include <cstdio>
#include <cstring>
#endif

#define COIN_NUM_MIN 0
#define COIN_NUM_MAX 999
#define COIN_STEP_NUM 50
#define PURPLE_COIN_POWER_STAR_APPEAR_NUM 100
#define STAR_PIECE_STEP_NUM 50

namespace {
    void requestOneUpIfNeed(int prevNum, int currNum, int stepNum) NO_INLINE {
        if (prevNum / stepNum < currNum / stepNum) {
            MR::incPlayerLeft();
            MR::requestOneUp();
        }
    }
};  // namespace

void ScenePlayingResult::incCoin(int num) {
#ifdef TARGET_PC
    port_board_pickup(num, 0);  // leaderboard (P5-07)
#endif
    s32 prevNum = mCoinNum;
    s32 currNum = MR::clamp(mCoinNum + num, COIN_NUM_MIN, COIN_NUM_MAX);
    mCoinNum = currNum;

    ::requestOneUpIfNeed(prevNum, currNum, COIN_STEP_NUM);
}

s32 ScenePlayingResult::getCoinNum() const {
    return MR::clamp(mCoinNum, COIN_NUM_MIN, COIN_NUM_MAX);
}

void ScenePlayingResult::incPurpleCoin() {
    mPurpleCoinNum++;

    if (mPurpleCoinNum == PURPLE_COIN_POWER_STAR_APPEAR_NUM) {
        MR::requestAppearPowerStarCoin100();
    }
}

void ScenePlayingResult::incStarPiece(int num) {
#ifdef TARGET_PC
    port_board_pickup(0, num);  // leaderboard (P5-07)
#endif
    s32 prevNum = GameDataFunction::getStarPieceNum();
    GameDataFunction::addStarPiece(num);
    s32 currNum = GameDataFunction::getStarPieceNum();
    s32 last1upNum = GameDataFunction::getLast1upStarPieceNum();

    if (MR::isStageAstroLocation()) {
        return;
    }

    if (last1upNum + STAR_PIECE_STEP_NUM > currNum) {
        return;
    }

    if (prevNum / STAR_PIECE_STEP_NUM < currNum / STAR_PIECE_STEP_NUM) {
        MR::incPlayerLeft();
        MR::requestOneUp();
    }

    GameDataFunction::setLast1upStarPieceNum(last1upNum + STAR_PIECE_STEP_NUM);
}

s32 ScenePlayingResult::getStarPieceNum() const {
    return GameDataFunction::getStarPieceNum();
}

ScenePlayingResult::~ScenePlayingResult() {
}

ScenePlayingResult::ScenePlayingResult() : NameObj("\x83\x76\x83\x8c\x83\x43\x8c\x8b\x89\xca\x95\xdb\x8e\x9d"), mCoinNum(0), mPurpleCoinNum(0) {
}

#ifdef TARGET_PC
// Leaderboard (P5-07, port/board.h).  Only gains count: shooting or feeding
// star bits takes them away, which is not a pickup.
namespace {
    PortBoard sBoard;

    void startRun(const char* stage, s32 scenario) {
        sBoard.prev = sBoard.cur;
        sBoard.cur.coins = 0;
        sBoard.cur.bits = 0;
        sBoard.cur.scenario = scenario;
        if (stage != sBoard.cur.stage) {
            snprintf(sBoard.cur.stage, sizeof(sBoard.cur.stage), "%s", stage);
        }
        sBoard.run++;
    }
}  // namespace

extern "C" PortBoard* port_board(void) { return &sBoard; }

extern "C" void port_board_pickup(int coins, int bits) {
    if (coins > 0) sBoard.cur.coins += coins;
    if (bits > 0) sBoard.cur.bits += bits;
}

extern "C" void port_board_stage(const char* stage, int scenario) {
    if (stage == nullptr || (scenario == sBoard.cur.scenario && strncmp(stage, sBoard.cur.stage, sizeof(sBoard.cur.stage) - 1) == 0)) {
        return;
    }
    startRun(stage, scenario);
}

extern "C" void port_board_star(void) {
    sBoard.stars++;
    startRun(sBoard.cur.stage, sBoard.cur.scenario);
}
#endif
