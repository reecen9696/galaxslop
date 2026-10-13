// Co-op session: see the header.
#include "net_client.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "net/client/progress.h"
#include "net/client/ws_client.h"
#include "net/client/ws_web.h"
#include "net/proto/player_state.h"
#include "port/heap_routing.h"
#include "port/port.h"

namespace net {

namespace {

constexpr uint8_t kMsgHello = 0x01, kMsgWelcome = 0x02, kMsgJoined = 0x03, kMsgLeft = 0x04, kMsgPing = 0x30, kMsgPong = 0x31, kMsgError = 0x40;
constexpr uint8_t kProtoVersion = 1;
constexpr int64_t kRenderDelayNs = 100'000'000;  // puppets show the past by 100 ms
constexpr int64_t kFreezeNs = 500'000'000;       // no sample this long: hold the last pose
constexpr int64_t kHideNs = 3'000'000'000;       // this long: hide
constexpr int kRing = 32;

struct Sample {
    int64_t recvNs;
    PlayerState s;
};

struct Remote {
    bool present = false;
    uint8_t id = 0;
    std::string name;
    bool luigi = false;
    uint8_t tint = 0;
    Sample ring[kRing];
    uint32_t count = 0;  // samples received
};

struct Session {
    std::shared_ptr<Transport> transport;  // shared with the keepalive thread
    std::atomic<int> state{0};  // 0 idle, 1 connecting, 2 open, 3 failed
    std::thread connector;
    uint8_t you = 0;
    std::string room;
    Remote remotes[kMaxRemotes];
    RemoteView views[kMaxRemotes];
    uint32_t frames = 0;
    int64_t lastPingNs = 0;
    float rttMs = -1.0f;
};

Session& session() {
    static Session* s = [] {
        PortHostAllocScope host;
        return new Session();
    }();
    return *s;
}

const char* envOr(const char* name, const char* fallback) {
    const char* v = getenv(name);
    return v && *v ? v : fallback;
}

void sendHello(Session& s) {
    std::string name = envOr("SMG_NAME", "Player");
    std::string room = envOr("SMG_ROOM", "");
    std::vector<uint8_t> b;
    b.push_back(kMsgHello);
    b.push_back(kProtoVersion);
    b.push_back((uint8_t)std::min<size_t>(name.size(), 32));
    b.insert(b.end(), name.begin(), name.begin() + std::min<size_t>(name.size(), 32));
    b.push_back(0);  // character: P2-06's lobby chooses; Mario for now
    b.push_back((uint8_t)atoi(envOr("SMG_TINT", "0")));  // tint
    b.push_back((uint8_t)room.size());
    b.insert(b.end(), room.begin(), room.end());
    s.transport->send(b.data(), b.size());
}

Remote* remoteById(Session& s, uint8_t id, bool create) {
    for (Remote& r : s.remotes) {
        if (r.present && r.id == id) return &r;
    }
    if (!create) return nullptr;
    for (Remote& r : s.remotes) {
        if (!r.present) {
            r = Remote();
            r.present = true;
            r.id = id;
            return &r;
        }
    }
    return nullptr;
}

std::string readStr(const uint8_t*& p, const uint8_t* end) {
    if (p >= end) return "";
    size_t n = *p++;
    if ((size_t)(end - p) < n) n = (size_t)(end - p);
    std::string s((const char*)p, n);
    p += n;
    return s;
}

void handle(Session& s, const std::vector<uint8_t>& m) {
    if (m.empty()) return;
    const uint8_t* p = m.data() + 1;
    const uint8_t* end = m.data() + m.size();
    switch (m[0]) {
    case kMsgWelcome: {
        if (p >= end) return;
        s.you = *p++;
        s.room = readStr(p, end);
        uint8_t n = p < end ? *p++ : 0;
        for (int i = 0; i < n && p < end; i++) {
            uint8_t id = *p++;
            std::string name = readStr(p, end);
            uint8_t character = p < end ? *p++ : 0;
            uint8_t tint = p < end ? *p++ : 0;
            if (id == s.you) continue;
            if (Remote* r = remoteById(s, id, true)) {
                r->name = name;
                r->luigi = character == 1;
                r->tint = tint;
            }
        }
        port_log("net: joined room %s as player %u (%u others)", s.room.c_str(), s.you, n ? n - 1 : 0);
        break;
    }
    case kMsgJoined: {
        if (p >= end) return;
        uint8_t id = *p++;
        std::string name = readStr(p, end);
        uint8_t character = p < end ? *p++ : 0;
        uint8_t tint = p < end ? *p++ : 0;
        if (Remote* r = remoteById(s, id, true)) {
            r->name = name;
            r->luigi = character == 1;
            r->tint = tint;
            port_log("net: %s joined (player %u)", name.c_str(), id);
        }
        break;
    }
    case kMsgLeft:
        if (p < end) {
            if (Remote* r = remoteById(s, *p, false)) {
                port_log("net: %s left", r->name.c_str());
                r->present = false;
            }
        }
        break;
    case kMsgPlayerState: {
        uint8_t id;
        PlayerState st;
        if (!decodeForwardedPlayerState(m.data(), m.size(), &id, &st) || id == s.you) return;
        if (Remote* r = remoteById(s, id, true)) {
            r->ring[r->count % kRing] = {port_host_time_ns(), st};
            r->count++;
        }
        break;
    }
    case kMsgPong:
        if (m.size() >= 5) {
            uint32_t sent;
            memcpy(&sent, m.data() + 1, 4);
            s.rttMs = (float)((uint32_t)(port_host_time_ns() / 1000000) - sent);
        }
        break;
    case kMsgError: {
        uint8_t code = p < end ? *p++ : 0;
        std::string msg = readStr(p, end);
        port_log("net: relay error %u: %s", code, msg.c_str());
        break;
    }
    case 0x21:  // ProgressLog (P3-02)
        progressOnLog(m.data(), m.size());
        break;
    default:
        break;
    }
}

// Interpolates remote `r` at `t` into `out`; false if it has nothing usable.
bool interpolate(const Remote& r, int64_t t, int64_t now, NetPlayerCapture* out) {
    if (r.count == 0) return false;
    const Sample& newest = r.ring[(r.count - 1) % kRing];
    if (now - newest.recvNs > kHideNs) return false;
    // The two samples around t (newest first).
    const Sample* after = &newest;
    const Sample* before = nullptr;
    uint32_t n = r.count < (uint32_t)kRing ? r.count : (uint32_t)kRing;
    for (uint32_t i = 1; i <= n; i++) {
        const Sample& c = r.ring[(r.count - i) % kRing];
        if (c.recvNs <= t) {
            before = &c;
            break;
        }
        after = &c;
    }
    const PlayerState* a;
    const PlayerState* b;
    float f;
    if (before == nullptr) {  // everything is newer than t: show the oldest
        a = b = &after->s;
        f = 0.0f;
    } else if (before == &newest || now - newest.recvNs > kFreezeNs) {  // nothing newer yet, or stalled: hold
        a = b = &before->s;
        f = 0.0f;
    } else {
        a = &before->s;
        b = &after->s;
        int64_t span = after->recvNs - before->recvNs;
        f = span > 0 ? (float)(t - before->recvNs) / (float)span : 1.0f;
        f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    }
    memset(out, 0, sizeof(*out));
    out->frame = b->tick;
    out->timeNs = t;
    for (int i = 0; i < 3; i++) {
        out->pos[i] = a->pos[i] + (b->pos[i] - a->pos[i]) * f;
        out->vel[i] = a->vel[i] + (b->vel[i] - a->vel[i]) * f;
        out->up[i] = a->up[i] + (b->up[i] - a->up[i]) * f;
    }
    // Quaternion nlerp along the short way.
    float qa[4], qb[4], q[4];
    memcpy(qa, a->rot, sizeof(qa));
    memcpy(qb, b->rot, sizeof(qb));
    float dot = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
    if (dot < 0.0f) {
        for (float& v : qb) v = -v;
    }
    for (int i = 0; i < 4; i++) q[i] = qa[i] + (qb[i] - qa[i]) * f;
    quatToMatrix(q, out->baseMtx);
    for (int i = 0; i < 3; i++) out->baseMtx[i][3] = out->pos[i];
    out->front[0] = out->baseMtx[0][2], out->front[1] = out->baseMtx[1][2], out->front[2] = out->baseMtx[2][2];
    // The animation changes with the newer sample; weights blend.
    out->animHash = b->animHash;
    // Frame: interpolated within one animation (packets come every 2nd
    // frame; the newest alone makes slow animations like the idle sway step);
    // across a loop wrap or an animation change, the newer one.
    out->animFrame = a->animHash == b->animHash && b->animFrame >= a->animFrame ? a->animFrame + (b->animFrame - a->animFrame) * f
                                                                                 : b->animFrame;
    out->animRate = 1.0f;
    for (int i = 0; i < 4; i++) out->animWeights[i] = a->animWeights[i] + (b->animWeights[i] - a->animWeights[i]) * f;
    out->stageHash = b->stageHash;
    out->scenario = b->scenario;
    out->flags = (b->flags & NET_STATE_LUIGI ? NET_CAPTURE_LUIGI : 0) | (b->flags & NET_STATE_GROUNDED ? NET_CAPTURE_GROUNDED : 0) |
                 (b->flags & NET_STATE_HIDDEN ? NET_CAPTURE_HIDDEN : 0) | (b->flags & NET_STATE_IN_DEMO ? NET_CAPTURE_IN_DEMO : 0) |
                 (((b->flags & NET_STATE_EYELID_MASK) >> kNetStateEyelidShift) << NET_CAPTURE_EYELID_SHIFT) |
                 (((b->flags & NET_STATE_MODE_MASK) >> kNetStateModeShift) << NET_CAPTURE_MODE_SHIFT);
    return true;
}

PlayerState toState(const NetPlayerCapture& c) {
    PlayerState s;
    s.tick = c.frame;
    memcpy(s.pos, c.pos, sizeof(s.pos));
    matrixToQuat(c.baseMtx, s.rot);
    memcpy(s.up, c.up, sizeof(s.up));
    memcpy(s.vel, c.vel, sizeof(s.vel));
    s.animHash = c.animHash;
    memcpy(s.animWeights, c.animWeights, sizeof(s.animWeights));
    s.animFrame = c.animFrame;
    s.flags = (c.flags & NET_CAPTURE_LUIGI ? NET_STATE_LUIGI : 0) | (c.flags & NET_CAPTURE_GROUNDED ? NET_STATE_GROUNDED : 0) |
              (c.flags & NET_CAPTURE_HIDDEN ? NET_STATE_HIDDEN : 0) | (c.flags & NET_CAPTURE_IN_DEMO ? NET_STATE_IN_DEMO : 0) |
              (((c.flags & NET_CAPTURE_EYELID_MASK) >> NET_CAPTURE_EYELID_SHIFT) << kNetStateEyelidShift) |
              (((c.flags & NET_CAPTURE_MODE_MASK) >> NET_CAPTURE_MODE_SHIFT) << kNetStateModeShift);
    s.stageHash = c.stageHash;
    s.scenario = (uint8_t)c.scenario;
    return s;
}

}  // namespace

bool netEnabled() {
    const char* relay = getenv("SMG_RELAY");
    return relay && *relay;
}

const RemoteView& netRemote(int slot) { return session().views[slot]; }

const char* netRoomCode() { return session().room.c_str(); }

uint8_t netLocalId() { return session().you; }

bool netPump() {
    if (!netEnabled()) return false;
    PortHostAllocScope host;  // the session's containers are not the game's to pay for
    Session& s = session();

    if (s.state.load() == 0) {
        // Connect from a helper thread: the game's frame must never block on DNS or TCP.
        s.state = 1;
        s.connector = std::thread([&s] {
            PortHostAllocScope scope;
            std::string error, url = getenv("SMG_RELAY");
#if defined(__EMSCRIPTEN__)
            auto ws = std::make_shared<WebWsClient>();  // the page's WebSocket (P2-03 web half)
#else
            auto ws = std::make_shared<WsClient>();
#endif
            if (!ws->connect(url, &error)) {
                port_log("net: %s", error.c_str());
                s.state = 3;
                return;
            }
            s.transport = ws;
            sendHello(s);
            port_log("net: connected to %s", url.c_str());
            s.state = 2;
            // Keepalive: netTick only runs during gameplay, and a long load,
            // cutscene or the prologue's story pages would otherwise go
            // silent past the relay's idle drop (15 s).
            std::shared_ptr<Transport> t = s.transport;
            while (t->isOpen()) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                uint8_t ping[5] = {kMsgPing};
                uint32_t ms = (uint32_t)(port_host_time_ns() / 1000000);
                memcpy(ping + 1, &ms, 4);
                t->send(ping, sizeof(ping));
            }
        });
        s.connector.detach();
    }
    if (s.state.load() != 2) return false;
    if (!s.transport->isOpen()) {
        // Dropped (relay restart, network): forget the room and join again.
        port_log("net: connection lost; reconnecting");
        s.transport.reset();
        for (Remote& r : s.remotes) r.present = false;
        for (RemoteView& v : s.views) v = RemoteView();
        s.you = 0;
        s.state = 0;
        return false;
    }

    std::vector<uint8_t> m;
    while (s.transport->poll(m)) handle(s, m);
    progressPump(*s.transport, port_host_time_ns());  // shared progress: send, resend, ack (P3-02)
    return true;
}

void netTick(const NetPlayerCapture* local) {
    if (!netPump()) return;
    PortHostAllocScope host;
    Session& s = session();

    int64_t now = port_host_time_ns();
    if (local != nullptr && s.you != 0 && (s.frames++ & 1) == 0) {  // every second frame: 30 Hz
        uint8_t wire[kPlayerStateSize];
        encodePlayerState(toState(*local), wire);
        s.transport->send(wire, sizeof(wire));
    }
    if (now - s.lastPingNs > 2'000'000'000) {
        s.lastPingNs = now;
        uint8_t ping[5] = {kMsgPing};
        uint32_t ms = (uint32_t)(now / 1000000);
        memcpy(ping + 1, &ms, 4);
        s.transport->send(ping, sizeof(ping));
    }

    for (int i = 0; i < kMaxRemotes; i++) {
        const Remote& r = s.remotes[i];
        RemoteView& v = s.views[i];
        v.present = r.present;
        v.id = r.id;
        snprintf(v.name, sizeof(v.name), "%s", r.name.c_str());
        // The state packets say what the save actually plays; Hello's
        // character is a lobby choice (P2-06) and 0 until then.
        v.luigi = r.count ? (r.ring[(r.count - 1) % kRing].s.flags & NET_STATE_LUIGI) != 0 : r.luigi;
        v.tint = r.tint;
        v.visible = r.present && interpolate(r, now - kRenderDelayNs, now, &v.pose) && local != nullptr &&
                    v.pose.stageHash == local->stageHash && v.pose.scenario == local->scenario &&
                    !(v.pose.flags & (NET_CAPTURE_HIDDEN | NET_CAPTURE_IN_DEMO));
    }
    static const bool sLog = getenv("PETARI_NETLOG") != nullptr;
    static uint32_t sN = 0;
    if (sLog && (sN++ % 120) == 0) {
        for (int i = 0; i < kMaxRemotes; i++) {
            const RemoteView& v = s.views[i];
            if (v.present) {
                port_log("net: room %s rtt %.0f ms; %s (%u) %s at %.0f %.0f %.0f", s.room.c_str(), s.rttMs, v.name, v.id,
                         v.visible ? "visible" : "not here", v.pose.pos[0], v.pose.pos[1], v.pose.pos[2]);
            }
        }
    }
}

}  // namespace net

extern "C" void port_net_pump(void) { net::netPump(); }
