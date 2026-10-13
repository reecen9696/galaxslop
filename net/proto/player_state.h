// PlayerState on the wire (docs/NETCODE.md, P2-01).  Pure C++, no game
// dependency: the same code runs in the native and browser builds and in the
// unit tests.  Little-endian.
//
// Client -> server payload (54 bytes; the server inserts the sender id after
// the type byte when it forwards, 55 bytes):
//
//   u8   type          0x10
//   u32  tick          sender's capture frame counter
//   f32  pos[3]
//   f16  rot[4]        orientation quaternion xyzw (from the base matrix)
//   f16  up[3]         against gravity
//   f16  vel[3]
//   u32  anim_hash     MR::getHashCode(animation name) (D-018)
//   u8   anim_weight[4] x255
//   f16  anim_frame
//   u16  flags         NET_STATE_*
//   u32  stage_hash    MR::getHashCode(stage name)
//   u8   scenario
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace net {

constexpr uint8_t kMsgPlayerState = 0x10;
constexpr size_t kPlayerStateSize = 54;           // as the client sends it
constexpr size_t kPlayerStateForwardedSize = 55;  // as the server forwards it

enum : uint16_t {
    NET_STATE_LUIGI = 0x0001,
    NET_STATE_GROUNDED = 0x0002,
    NET_STATE_HIDDEN = 0x0004,
    NET_STATE_IN_DEMO = 0x0008,
    NET_STATE_EYELID_MASK = 0x0030,  // eyelid frame 0 (open) .. 3 (closed)
    NET_STATE_MODE_MASK = 0x03C0,    // power-up form (MarioActor's PlayerMode, 0-9)
};
constexpr int kNetStateEyelidShift = 4;
constexpr int kNetStateModeShift = 6;

struct PlayerState {
    uint32_t tick = 0;
    float pos[3] = {};
    float rot[4] = {0, 0, 0, 1};  // quaternion xyzw
    float up[3] = {0, 1, 0};
    float vel[3] = {};
    uint32_t animHash = 0;
    float animWeights[4] = {};
    float animFrame = 0;
    uint16_t flags = 0;
    uint32_t stageHash = 0;
    uint8_t scenario = 0;
};

// Contact (D-020): one player touched another's puppet.  Client -> server
// [0x12][u8 target_id][u8 kind][f16 dir[3]]; the server forwards it to the
// target only, with the sender's id in place of the target's.
constexpr uint8_t kMsgContact = 0x12;
constexpr size_t kContactSize = 9;
enum : uint8_t {
    kContactTrample = 1,  // stomped on (the target flinches)
};
struct Contact {
    uint8_t playerId = 0;  // target when sending, sender when received
    uint8_t kind = 0;
    float dir[3] = {};     // from the toucher towards the touched, unit length
};
void encodeContact(const Contact& c, uint8_t* out);           // kContactSize bytes
bool decodeContact(const uint8_t* in, size_t len, Contact* c);  // false if malformed

uint16_t floatToHalf(float f);
float halfToFloat(uint16_t h);

// Writes kPlayerStateSize bytes.
void encodePlayerState(const PlayerState& s, uint8_t* out);
// Reads a forwarded message (type, sender id, payload); false if malformed.
bool decodeForwardedPlayerState(const uint8_t* in, size_t len, uint8_t* senderId, PlayerState* s);

// Rotation matrix (3x3, columns = side, up, front) <-> unit quaternion xyzw.
void matrixToQuat(const float m[3][4], float q[4]);
void quatToMatrix(const float q[4], float m[3][4]);  // leaves column 3 alone

}  // namespace net
