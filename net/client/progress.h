// Shared progress (P3-02, docs/NETCODE.md "Progress merge", D-022): local
// events go to the relay until its ProgressLog contains them; the log's
// entries come back to the game to apply.  The game talks to this through
// the C functions at the bottom (decomp/ calls them under TARGET_PC).
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
namespace net {

class Transport;

enum ProgressKind : uint16_t {
    kProgressStar = 1,       // stage_hash = galaxy, scenario, arg 0
    kProgressStoryFlag = 2,  // arg = flag id (P3-03)
    kProgressGalaxyUnlock = 3,
    kProgressDomeUnlock = 4,
    kProgressGrandStar = 5,  // stage_hash 0, arg = grand star index (SpecialStarGrand<n>)
};

// net_client.cpp: a ProgressLog message arrived (type byte included).
void progressOnLog(const uint8_t* msg, size_t len);
// net_client.cpp, every netPump while connected: (re)sends pending events
// and acks what was applied.
void progressPump(Transport& t, int64_t nowNs);

}  // namespace net
extern "C" {
#endif

typedef struct PortNetProgress {
    uint8_t origin;  // player id that caused it (the local one too: its own echo)
    uint16_t kind;
    uint32_t stageHash;
    uint8_t scenario;
    uint16_t arg;
} PortNetProgress;

// Game thread: something happened locally that the room shares.
void port_net_progress_send(uint16_t kind, uint32_t stageHash, uint8_t scenario, uint16_t arg);
// Game thread: next logged event to apply (local save lacks it -> set it); 0 if none.
int port_net_progress_poll(PortNetProgress* out);
// The game is applying a logged event (its hooks must not send it again).
void port_net_progress_applying(int on);
int port_net_progress_is_applying(void);
// A player's name by relay id ("a player" if not in the room now).
const char* port_net_player_name(uint8_t id);

#ifdef __cplusplus
}
#endif
