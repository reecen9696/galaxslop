// See progress.h.
#include "net/client/progress.h"

#include <string.h>

#include <deque>
#include <mutex>
#include <vector>

#include "net/client/net_client.h"
#include "net/client/ws_client.h"
#include "port/heap_routing.h"
#include "port/port.h"

namespace net {
namespace {

constexpr uint8_t kMsgProgressEvent = 0x20;
constexpr uint8_t kMsgProgressLog = 0x21;
constexpr uint8_t kMsgProgressAck = 0x22;
constexpr uint8_t kMsgProgressSnapshot = 0x23;
constexpr int64_t kResendNs = 2000000000ll;
constexpr size_t kLogEntrySize = 14;  // u32 seq, u8 origin, u16 kind, u32 stage, u8 scenario, u16 arg

struct Event {
    uint16_t kind;
    uint32_t stageHash;
    uint8_t scenario;
    uint16_t arg;
    bool operator==(const Event& o) const { return kind == o.kind && stageHash == o.stageHash && scenario == o.scenario && arg == o.arg; }
};

std::mutex sLock;
std::vector<Event> sPending;         // sent, not yet seen in the log
std::deque<PortNetProgress> sInbox;  // logged, not yet applied by the game
int64_t sApplied = -1;               // highest contiguous seq taken from the log
bool sAckDue = false;
int64_t sLastSend = 0;
bool sSendNow = false;
std::vector<uint8_t> sOwnSnap;   // this client's progress, as last offered
bool sOwnSnapDue = false;        // not yet sent to this room
std::vector<uint8_t> sHostSnap;  // the host's progress (P3-07)
uint32_t sHostSnapVersion = 0;
uint8_t sHostSnapOrigin = 0;

uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

}  // namespace

void progressOnLog(const uint8_t* m, size_t len) {
    if (len < 7 || m[0] != kMsgProgressLog) return;
    uint32_t from = rd32(m + 1);
    uint16_t count = rd16(m + 5);
    if (len < 7 + (size_t)count * kLogEntrySize) return;
    PortHostAllocScope hostAlloc;
    std::lock_guard<std::mutex> lk(sLock);
    if (from == 0 && count > 0) {
        sApplied = -1;  // a replay from the start (join, rejoin, another room): applying is idempotent
    }
    for (uint16_t i = 0; i < count; i++) {
        const uint8_t* e = m + 7 + i * kLogEntrySize;
        uint32_t seq = rd32(e);
        if ((int64_t)seq <= sApplied) continue;     // already have it
        if ((int64_t)seq != sApplied + 1) break;    // a gap: the relay resends from our ack
        PortNetProgress p;
        p.origin = e[4];
        p.kind = rd16(e + 5);
        p.stageHash = rd32(e + 7);
        p.scenario = e[11];
        p.arg = rd16(e + 12);
        sInbox.push_back(p);
        Event ev{p.kind, p.stageHash, p.scenario, p.arg};
        for (size_t k = 0; k < sPending.size(); k++) {
            if (sPending[k] == ev) {
                sPending.erase(sPending.begin() + (long)k);
                break;
            }
        }
        sApplied = seq;
        sAckDue = true;
    }
}

void progressOnSnapshot(const uint8_t* m, size_t len) {
    if (len < 4 || m[0] != kMsgProgressSnapshot) return;
    size_t n = (size_t)(m[2] | m[3] << 8);
    if (n == 0 || n > PORT_NET_SNAPSHOT_MAX || len != 4 + n) return;
    PortHostAllocScope hostAlloc;
    std::lock_guard<std::mutex> lk(sLock);
    sHostSnap.assign(m + 4, m + 4 + n);
    sHostSnapOrigin = m[1];
    sHostSnapVersion++;
    port_log("net: host progress from player %u (%u bytes)", m[1], (unsigned)n);
}

void progressOnJoined() {
    PortHostAllocScope hostAlloc;
    std::lock_guard<std::mutex> lk(sLock);
    sOwnSnapDue = !sOwnSnap.empty();
    sHostSnap.clear();
    sHostSnapVersion++;
}

void progressPump(Transport& t, int64_t nowNs) {
    std::vector<uint8_t> out;
    PortHostAllocScope hostAlloc;
    std::lock_guard<std::mutex> lk(sLock);
    if (sOwnSnapDue && netLocalId() != 0) {
        out.reserve(3 + sOwnSnap.size());
        out.push_back(kMsgProgressSnapshot);
        out.push_back((uint8_t)sOwnSnap.size());
        out.push_back((uint8_t)(sOwnSnap.size() >> 8));
        out.insert(out.end(), sOwnSnap.begin(), sOwnSnap.end());
        t.send(out.data(), out.size());
        sOwnSnapDue = false;
    }
    if (sAckDue && sApplied >= 0) {
        uint8_t ack[5] = {kMsgProgressAck, (uint8_t)sApplied, (uint8_t)(sApplied >> 8), (uint8_t)(sApplied >> 16), (uint8_t)(sApplied >> 24)};
        t.send(ack, sizeof(ack));
        sAckDue = false;
    }
    if (sPending.empty() || (!sSendNow && nowNs - sLastSend < kResendNs)) return;
    for (const Event& e : sPending) {
        uint8_t b[10] = {kMsgProgressEvent,
                         (uint8_t)e.kind, (uint8_t)(e.kind >> 8),
                         (uint8_t)e.stageHash, (uint8_t)(e.stageHash >> 8), (uint8_t)(e.stageHash >> 16), (uint8_t)(e.stageHash >> 24),
                         e.scenario,
                         (uint8_t)e.arg, (uint8_t)(e.arg >> 8)};
        t.send(b, sizeof(b));
    }
    sLastSend = nowNs;
    sSendNow = false;
}

}  // namespace net

extern "C" void port_net_progress_send(uint16_t kind, uint32_t stageHash, uint8_t scenario, uint16_t arg) {
    PortHostAllocScope hostAlloc;
    std::lock_guard<std::mutex> lk(net::sLock);
    net::Event ev{kind, stageHash, scenario, arg};
    for (const net::Event& e : net::sPending) {
        if (e == ev) return;
    }
    net::sPending.push_back(ev);
    net::sSendNow = true;
    port_log("net: progress kind %u stage %08x/%u arg %u queued", kind, stageHash, scenario, arg);
}

extern "C" int port_net_progress_poll(PortNetProgress* out) {
    PortHostAllocScope hostAlloc;
    std::lock_guard<std::mutex> lk(net::sLock);
    if (net::sInbox.empty()) return 0;
    *out = net::sInbox.front();
    net::sInbox.pop_front();
    return 1;
}

// Set while the game applies a logged event, so its own hooks don't send it
// back as a local one.
static int sApplying = 0;
extern "C" void port_net_progress_applying(int on) { sApplying = on; }
extern "C" int port_net_progress_is_applying(void) { return sApplying; }

extern "C" void port_net_snapshot_offer(const uint8_t* data, uint32_t len) {
    if (len == 0 || len > PORT_NET_SNAPSHOT_MAX) return;
    PortHostAllocScope hostAlloc;
    std::lock_guard<std::mutex> lk(net::sLock);
    if (net::sOwnSnap.size() == len && memcmp(net::sOwnSnap.data(), data, len) == 0) return;
    net::sOwnSnap.assign(data, data + len);
    net::sOwnSnapDue = true;
}

extern "C" uint32_t port_net_host_snapshot(uint8_t* out, uint32_t cap, uint32_t* version, uint8_t* origin) {
    PortHostAllocScope hostAlloc;
    std::lock_guard<std::mutex> lk(net::sLock);
    if (version) *version = net::sHostSnapVersion;
    if (origin) *origin = net::sHostSnapOrigin;
    if (net::sHostSnap.empty() || net::sHostSnap.size() > cap) return 0;
    memcpy(out, net::sHostSnap.data(), net::sHostSnap.size());
    return (uint32_t)net::sHostSnap.size();
}

extern "C" int port_net_in_room(void) { return net::netLocalId() != 0; }

extern "C" const char* port_net_player_name(uint8_t id) {
    for (int i = 0; i < net::kMaxRemotes; i++) {
        const net::RemoteView& v = net::netRemote(i);
        if (v.present && v.id == id) return v.name;
    }
    return "a player";  // left the room since (or this is our own echo, which is never applied)
}
