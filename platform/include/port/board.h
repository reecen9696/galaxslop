// Leaderboard (P5-07): the coins and star bits the local player picked up
// (not what other players' pickups do here: world sync takes those without
// touching the counters), per run, for the page to read.  A run is one visit
// to a galaxy's star: it ends on a stage or scenario change and on a star.
// Written on the game thread (ScenePlayingResult.cpp, GameDataHolder.cpp,
// GameDataFunction.cpp), read by the page between frames (present_web.cpp).
#pragma once

#include <stdint.h>

struct PortBoardRun {
    uint32_t coins;
    uint32_t bits;
    int32_t scenario;
    char stage[48];
};

struct PortBoard {
    uint32_t run;    // bumped when a run starts
    uint32_t stars;  // stars the local player got
    PortBoardRun cur;
    PortBoardRun prev;  // the run that just ended, with its final counts
};

#ifdef __cplusplus
extern "C" {
#endif
PortBoard* port_board(void);
void port_board_pickup(int coins, int bits);
void port_board_stage(const char* stage, int scenario);
void port_board_star(void);
#ifdef __cplusplus
}
#endif
