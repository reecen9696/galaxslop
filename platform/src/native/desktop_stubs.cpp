// Flat-screen meanings for the hooks GalaxyQuest's VR layer (platform/src/xr,
// not built on desktop) provides.  The game calls them
// from TARGET_PC code; on a flat screen there is no headset view, diorama,
// laser pointer or VR transit, so every query answers "not in VR" and the
// game takes its original Wii path.
#include <stdint.h>

#include <atomic>

// Frames shown so far (gpu_null.cpp and the recorder store it; the frame
// pacer and tools read it).
std::atomic<uint64_t> gPresentedFrames{0};

extern "C" {

int port_vr_cull_view(float*, float*, float*) { return 0; }
int port_vr_move_axes(float*, float*, float*) { return 0; }
int port_vr_pointer_in_world(void) { return 0; }
int port_vr_pointer_ray(float*, float*) { return 0; }
void port_vr_pointer_reach(float) {}
void port_vr_pointer_depth(float) {}
void port_vr_pointer_touched(unsigned long long) {}
void port_vr_wipe(int, float, unsigned int) {}
int port_vr_diorama(void) { return 0; }
void port_vr_transit_begin(const void*) {}
void port_vr_transit_end(const void*) {}
const void* port_vr_transit_host(void) { return nullptr; }
void port_vr_pause_menu(int) {}

}  // extern "C"

