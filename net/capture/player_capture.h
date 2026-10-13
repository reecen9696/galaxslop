// The local player's state, captured once per gameplay frame (P2-04, D-018).
//
// GameScene::update calls port_net_capture_player() while gameplay runs;
// net/ reads the latest capture to build PlayerState packets
// (docs/NETCODE.md), and the loopback puppet (P2-05) replays the history.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NetPlayerCapture {
    uint32_t frame;       // gameplay frames captured since boot
    int64_t timeNs;       // port_host_time_ns() at capture
    float pos[3];         // world position
    float vel[3];         // velocity (world units per frame)
    float baseMtx[3][4];  // Mario's base matrix: orientation (columns: side, up, front) + translation
    float up[3];          // against gravity
    float front[3];       // facing
    uint32_t animHash;    // MR::getHashCode(current animation name)
    float animFrame;      // frame within that animation
    float animRate;       // playback rate
    float animWeights[4]; // track blend weights; the basic locomotion group mixes stand/walk/run/dash this way
    uint32_t stageHash;   // MR::getHashCode(MR::getCurrentStageName())
    int32_t scenario;     // MR::getCurrentScenarioNo()
    uint16_t flags;       // NET_CAPTURE_*
} NetPlayerCapture;

#define NET_CAPTURE_LUIGI 0x0001
#define NET_CAPTURE_GROUNDED 0x0002
#define NET_CAPTURE_HIDDEN 0x0004
#define NET_CAPTURE_IN_DEMO 0x0008
// Eyelid texture 0 (open) .. 3 (closed): MarioActor's blink and eye
// animations (sleeping), which the animation itself does not carry.
#define NET_CAPTURE_EYELID_MASK 0x0030
#define NET_CAPTURE_EYELID_SHIFT 4
// Power-up form: MarioActor's PlayerMode (0 normal, 1 rainbow, 2 fire,
// 3 ice, 4 bee, 5 spring, 6 boo, 7 flying, 9 spin drill...).
#define NET_CAPTURE_MODE_MASK 0x03C0
#define NET_CAPTURE_MODE_SHIFT 6
#define NET_CAPTURE_SQUASHED 0x0400  // flattened (ground-pounded, crushed)

// Called by game code once per gameplay frame (Mario exists, scene in play).
void port_net_capture_player(void);

// Latest capture; returns 0 if nothing has been captured yet.
int net_capture_latest(NetPlayerCapture* out);

// The capture closest to (but not after) `timeNs`, from the last ~4 s;
// returns 0 if none is that old.
int net_capture_at(int64_t timeNs, NetPlayerCapture* out);

// Current animation name (for logs); empty if none.
const char* net_capture_anim_name(void);

#ifdef __cplusplus
}
#endif
