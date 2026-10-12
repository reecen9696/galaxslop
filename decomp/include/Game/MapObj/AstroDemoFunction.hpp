#pragma once

#ifdef TARGET_PC
// The functions are defined in Game/Demo/AstroDemoFunction.cpp as a namespace
// returning void; this older class declaration said bool.  On the Wii the
// mismatch was invisible, in WebAssembly the call becomes a trap.
#include "Game/Demo/AstroDemoFunction.hpp"
#else


#include <revolution/types.h>

class JMapInfoIter;
class LiveActor;

namespace MR {
    class FunctorBase;
};  // namespace MR

class AstroDemoFunction {
public:
    static s32 getOpenedAstroDomeNum();
    static const char* getGrandStarReturnDemoName(int);

    static bool tryRegisterDemo(LiveActor*, const char*, const JMapInfoIter&);
    static bool tryRegisterGrandStarReturnAndSimpleCast(LiveActor*, const JMapInfoIter&);
    static bool tryRegisterGrandStarReturnWithFunctionAndSimpleCast(LiveActor*, const JMapInfoIter&, const MR::FunctorBase&);
};
#endif
