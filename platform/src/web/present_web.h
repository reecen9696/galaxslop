// Browser presentation (platform/src/web/present_web.cpp).
#pragma once

#include "port/input.h"

bool web_present_init(int scale);
void web_present_frame();
unsigned long long web_present_frame_number();
// The keyboard and mouse as a Wii remote; call once per frame.
void web_read_input(PortPadState* pad);
