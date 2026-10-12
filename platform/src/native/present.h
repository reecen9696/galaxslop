// Desktop window + renderer (platform/src/native/present.cpp).
#pragma once

#include "port/input.h"

// Creates the window (hidden when headless), the GL 4.1 context and the EFB
// target at `scale` x the native 640x456.  Main thread only.
bool desktop_present_init(bool headless, int scale);
// Polls input, replays the newest recorded frame and shows it.  With
// useKeyboard, *pad receives the keyboard/mouse state.  Returns false when
// the window was closed (or F12 pressed).
bool desktop_present_frame(PortPadState* pad, bool useKeyboard);
// Writes the last rendered EFB as a PNG; returns the frame number, 0 if none.
unsigned long long desktop_present_shot(const char* path);
void desktop_present_shutdown();
