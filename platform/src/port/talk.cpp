// See port/talk.h.  Presses come from the input thread, the game takes them.
#include "port/talk.h"

#include <atomic>

namespace {
std::atomic<int> sPresses{0};
}  // namespace

extern "C" void port_talk_note_press(void) { sPresses.fetch_add(1); }

extern "C" int port_talk_take_press(void) { return sPresses.exchange(0) > 0; }
