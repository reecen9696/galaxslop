// Browser presentation (P0-08): a WebGL2 context on the canvas, which
// Emscripten transfers to the proxied main thread as an OffscreenCanvas, and
// the same gpu::Renderer the desktop uses (port/gl.h picks GLSL ES 3.00).
// web_present_frame runs from emscripten_set_main_loop (requestAnimationFrame
// on that worker), so the browser presents each frame when the callback
// returns.  An explicit-swap loop that never returned to the event loop
// stalled on emscripten_webgl_commit_frame.
#include "present_web.h"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>
#include <unordered_set>

#include "../gx/gl_renderer.h"
#include "net/client/net_client.h"
#include "port/port.h"

namespace {

const int kEfbW = 640, kEfbH = 456;
EMSCRIPTEN_WEBGL_CONTEXT_HANDLE sCtx = 0;
pthread_t sEventThread;  // runs the frame loop and html5/websocket callbacks
gpu::EfbTarget sEfb;

// Wii remote buttons (same bits as platform/src/native/present.cpp).
enum : uint16_t {
    kLeft = 0x0001, kRight = 0x0002, kDown = 0x0004, kUp = 0x0008, kPlus = 0x0010, kTwo = 0x0100, kOne = 0x0200,
    kB = 0x0400, kA = 0x0800, kMinus = 0x1000, kZ = 0x2000, kC = 0x4000, kHome = 0x8000,
};

// Input arrives as html5 events proxied to this thread (the one that runs
// the frame loop) whenever it returns to its event loop.
std::unordered_set<std::string> sKeys;  // KeyboardEvent.code values held
bool sPointerIn = false;
float sPointerX = 0, sPointerY = 0;  // -1..1 over the canvas
bool sMouseL = false, sMouseR = false;
bool sCursorActive = false;  // the gamepad's virtual pointer, until the mouse moves

bool isGameKey(const char* code) {
    static const char* const kKeys[] = {"KeyW", "KeyA", "KeyS", "KeyD", "Space", "KeyK", "ControlLeft", "ControlRight", "KeyC", "Enter",
                                        "Escape", "Backspace", "Digit1", "Digit2", "KeyH", "ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight",
                                        "KeyJ", "ShiftLeft"};
    for (const char* k : kKeys) {
        if (strcmp(code, k) == 0) return true;
    }
    return false;
}

bool onKey(int type, const EmscriptenKeyboardEvent* e, void*) {
    if (!isGameKey(e->code)) {
        return false;  // leave browser shortcuts alone
    }
    if (type == EMSCRIPTEN_EVENT_KEYDOWN) {
        sKeys.insert(e->code);
    } else {
        sKeys.erase(e->code);
    }
    return true;  // no page scrolling on Space / arrows
}

bool onBlur(int, const EmscriptenFocusEvent*, void*) {
    sKeys.clear();  // keys released while the page was not focused
    sMouseL = sMouseR = false;
    return false;
}

bool onMouse(int type, const EmscriptenMouseEvent* e, void*) {
    double cw = 0, ch = 0;
    emscripten_get_element_css_size("#canvas", &cw, &ch);
    if (type == EMSCRIPTEN_EVENT_MOUSELEAVE) {
        sPointerIn = false;
    } else if (cw > 0 && ch > 0) {
        sPointerIn = true;
        sCursorActive = false;
        sPointerX = (float)(e->targetX / cw * 2.0 - 1.0);
        sPointerY = (float)(e->targetY / ch * 2.0 - 1.0);
    }
    if (type == EMSCRIPTEN_EVENT_MOUSEDOWN || type == EMSCRIPTEN_EVENT_MOUSEUP) {
        bool down = type == EMSCRIPTEN_EVENT_MOUSEDOWN;
        if (e->button == 0) sMouseL = down;
        if (e->button == 2) sMouseR = down;
    }
    return type != EMSCRIPTEN_EVENT_MOUSEMOVE;
}

bool held(const char* code) { return sKeys.count(code) != 0; }

}  // namespace

// The thread that receives html5 and websocket callbacks (net/client/ws_web.cpp).
extern "C" pthread_t web_event_thread(void) { return sEventThread; }

static void applyScale();
static void recordTiming(double presentMs);
static void updateNameTags();

bool web_present_init(int scale) {
    sEventThread = pthread_self();
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.alpha = false;
    attrs.depth = false;  // the renderer draws into its own EFB targets
    attrs.stencil = false;
    attrs.antialias = false;
    attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
    sCtx = emscripten_webgl_create_context("#canvas", &attrs);
    if (sCtx <= 0) {
        port_log("present: WebGL2 context failed (%d)", (int)sCtx);
        return false;
    }
    emscripten_webgl_make_context_current(sCtx);
    if (!gpu::renderer().init()) {
        port_log("present: renderer init failed");
        return false;
    }
    int s = scale > 0 ? scale : 2;
    if (const char* env = getenv("SMG_SCALE")) {
        int v = atoi(env);
        if (v >= 1 && v <= 4) s = v;
    }
    sEfb = gpu::renderer().createTarget(kEfbW * s, kEfbH * s);
    emscripten_set_canvas_element_size("#canvas", kEfbW * s, kEfbH * s);
    port_log("present: WebGL2 ready, EFB %dx%d", sEfb.width, sEfb.height);

    const pthread_t self = EM_CALLBACK_THREAD_CONTEXT_CALLING_THREAD;
    emscripten_set_keydown_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, onKey, self);
    emscripten_set_keyup_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, onKey, self);
    emscripten_set_blur_callback_on_thread(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, onBlur, self);
    emscripten_set_mousemove_callback_on_thread("#canvas", nullptr, true, onMouse, self);
    emscripten_set_mousedown_callback_on_thread("#canvas", nullptr, true, onMouse, self);
    emscripten_set_mouseup_callback_on_thread("#canvas", nullptr, true, onMouse, self);
    emscripten_set_mouseleave_callback_on_thread("#canvas", nullptr, true, onMouse, self);
    return true;
}

void web_present_frame() {
    applyScale();
    double t0 = emscripten_get_now();
    gpu::Renderer& r = gpu::renderer();
    r.update();
    r.render(sEfb, nullptr, nullptr);
    int cw = 0, ch = 0;
    emscripten_get_canvas_element_size("#canvas", &cw, &ch);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, cw, ch);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sEfb.fbo);
    glBlitFramebuffer(0, 0, sEfb.width, sEfb.height, 0, 0, cw, ch, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    recordTiming(emscripten_get_now() - t0);
    updateNameTags();
}

// Gamepad (standard mapping) on top of keyboard/mouse, per BUILD_SPEC
// section 7.  The right stick moves a virtual pointer; once it moves, the
// pointer follows the stick until the mouse moves again.
static void readGamepad(PortPadState* pad) {
    static float sCursorX = 0, sCursorY = 0;
    if (emscripten_sample_gamepad_data() != EMSCRIPTEN_RESULT_SUCCESS) {
        return;
    }
    int n = emscripten_get_num_gamepads();
    for (int i = 0; i < n; i++) {
        EmscriptenGamepadEvent g;
        if (emscripten_get_gamepad_status(i, &g) != EMSCRIPTEN_RESULT_SUCCESS || !g.connected || strcmp(g.mapping, "standard") != 0) {
            continue;
        }
        auto btn = [&](int b) { return b < g.numButtons && g.digitalButton[b]; };
        auto axis = [&](int a) {
            float v = a < g.numAxes ? (float)g.axis[a] : 0.0f;
            return fabsf(v) < 0.15f ? 0.0f : v;  // dead zone
        };
        if (axis(0) != 0 || axis(1) != 0) {
            pad->stickX = axis(0);
            pad->stickY = -axis(1);
        }
        if (btn(0)) pad->buttons |= kA;
        if (btn(7)) pad->buttons |= kB;
        if (btn(6)) pad->buttons |= kZ;
        if (btn(4)) pad->buttons |= kC;
        if (btn(9)) pad->buttons |= kPlus;
        if (btn(8)) pad->buttons |= kMinus;
        if (btn(12)) pad->buttons |= kUp;
        if (btn(13)) pad->buttons |= kDown;
        if (btn(14)) pad->buttons |= kLeft;
        if (btn(15)) pad->buttons |= kRight;
        if (btn(16)) pad->buttons |= kHome;
        if (btn(1) || btn(2)) {
            static unsigned sFlip = 0;
            float a = (++sFlip & 1) ? 2.5f : -2.5f;
            pad->accX = a;
            pad->accY = a;
        }
        float rx = axis(2), ry = axis(3);
        if (rx != 0 || ry != 0) {
            if (!sCursorActive) {
                sCursorX = sPointerIn ? sPointerX : 0.0f;
                sCursorY = sPointerIn ? sPointerY : 0.0f;
                sCursorActive = true;
            }
            const float kSpeed = 0.03f;  // screen half-widths per frame at full tilt (P1-09 makes it a setting)
            sCursorX = fminf(1.0f, fmaxf(-1.0f, sCursorX + rx * kSpeed));
            sCursorY = fminf(1.0f, fmaxf(-1.0f, sCursorY + ry * kSpeed));
        }
        if (sCursorActive) {
            pad->pointerValid = 1;
            pad->pointerX = sCursorX;
            pad->pointerY = sCursorY;
        }
        break;  // first standard pad drives remote 0
    }
}

// Keyboard/mouse -> one Wii remote + Nunchuk, the desktop mapping
// (docs/BUILDING.md "Controls").
void web_read_input(PortPadState* pad) {
    memset(pad, 0, sizeof(*pad));
    pad->connected = 1;
    pad->pointerDist = 2.0f;
    pad->stickX = (held("KeyD") ? 1.0f : 0.0f) - (held("KeyA") ? 1.0f : 0.0f);
    pad->stickY = (held("KeyW") ? 1.0f : 0.0f) - (held("KeyS") ? 1.0f : 0.0f);
    if (held("Space")) pad->buttons |= kA;
    if (held("KeyK") || sMouseR) pad->buttons |= kB;
    if (held("ControlLeft") || held("ControlRight")) pad->buttons |= kZ;
    if (held("KeyC")) pad->buttons |= kC;
    if (held("Enter") || held("Escape")) pad->buttons |= kPlus;
    if (held("Backspace")) pad->buttons |= kMinus;
    if (held("Digit1")) pad->buttons |= kOne;
    if (held("Digit2")) pad->buttons |= kTwo;
    if (held("KeyH")) pad->buttons |= kHome;
    if (held("ArrowUp")) pad->buttons |= kUp;
    if (held("ArrowDown")) pad->buttons |= kDown;
    if (held("ArrowLeft")) pad->buttons |= kLeft;
    if (held("ArrowRight")) pad->buttons |= kRight;
    // Spin: a shake of the remote, alternating acceleration each frame.
    if (held("KeyJ") || held("ShiftLeft") || sMouseL) {
        static unsigned sFlip = 0;
        float a = (++sFlip & 1) ? 2.5f : -2.5f;
        pad->accX = a;
        pad->accY = a;
    }
    if (sPointerIn) {
        pad->pointerValid = 1;
        pad->pointerX = sPointerX;
        pad->pointerY = sPointerY;
    }
    readGamepad(pad);
}

// Render scale (P1-07): the page asks for a new EFB size (settings);
// applied between frames on this thread, which owns the GL context.
static std::atomic<int> sWantScale{0};
extern "C" EMSCRIPTEN_KEEPALIVE void web_request_scale(int scale) { sWantScale.store(scale); }

static void applyScale() {
    int want = sWantScale.exchange(0);
    if (want < 1 || want > 4 || sEfb.width == kEfbW * want) {
        return;
    }
    gpu::renderer().destroyTarget(sEfb);
    sEfb = gpu::renderer().createTarget(kEfbW * want, kEfbH * want);
    emscripten_set_canvas_element_size("#canvas", kEfbW * want, kEfbH * want);
    port_log("present: render scale %dx (%dx%d)", want, sEfb.width, sEfb.height);
}

// Frame timing (P1-07): CPU time of each present and the interval between
// presents, as p50/p95 over the last second, for the page's overlay.
struct FrameStats {
    float presentP50, presentP95, intervalP50, intervalP95;
    uint32_t fps, gameFrame;
};
static FrameStats sStats;
extern "C" EMSCRIPTEN_KEEPALIVE FrameStats* web_frame_stats(void) { return &sStats; }

static void recordTiming(double presentMs) {
    static std::vector<float> present, interval;
    static double last = 0, windowStart = 0;
    double now = emscripten_get_now();
    if (last > 0) interval.push_back((float)(now - last));
    last = now;
    present.push_back((float)presentMs);
    if (windowStart == 0) windowStart = now;
    if (now - windowStart < 1000) return;
    auto pct = [](std::vector<float>& v, double q) {
        if (v.empty()) return 0.0f;
        std::sort(v.begin(), v.end());
        return v[std::min(v.size() - 1, (size_t)(q * v.size()))];
    };
    sStats.fps = (uint32_t)present.size();
    sStats.presentP50 = pct(present, 0.5);
    sStats.presentP95 = pct(present, 0.95);
    sStats.intervalP50 = pct(interval, 0.5);
    sStats.intervalP95 = pct(interval, 0.95);
    float intervalMax = interval.empty() ? 0.0f : interval.back();  // sorted by pct()
    float presentMax = present.empty() ? 0.0f : present.back();
    sStats.gameFrame = (uint32_t)gpu::renderer().frameNumber();
    static const bool log = getenv("PETARI_PERFLOG") != nullptr;
    if (log) {
        port_log("web: %u fps, present %.1f/%.1f/%.1f ms, interval %.1f/%.1f/%.1f ms (p50/p95/max)", sStats.fps, sStats.presentP50,
                 sStats.presentP95, presentMax, sStats.intervalP50, sStats.intervalP95, intervalMax);
    }
    present.clear();
    interval.clear();
    windowStart = now;
}

// Name tags (P2-07): where each visible remote player's head is on screen,
// for the page to draw HTML labels over the canvas (web/app.js).  Uses the
// camera of the frame just drawn, so tags move with the picture.
struct NameTag {
    float x, y;  // normalized device coordinates (-1..1, y up)
    uint32_t tint;
    char name[36];
};
struct NameTags {
    uint32_t count;
    NameTag tags[net::kMaxRemotes];
};
static NameTags sTags;
extern "C" EMSCRIPTEN_KEEPALIVE NameTags* web_name_tags(void) { return &sTags; }

static void updateNameTags() {
    NameTags out{};
    const gpu::CameraInfo& cam = gpu::renderer().camera();
    if (cam.valid && net::netEnabled()) {
        float t = tanf(cam.fovy * 0.5f * 3.14159265f / 180.0f);
        float aspect = cam.aspect > 0 ? cam.aspect : 640.0f / 456.0f;
        for (int i = 0; i < net::kMaxRemotes; i++) {
            const net::RemoteView& v = net::netRemote(i);
            if (!v.present || !v.visible) continue;
            // A little above the head, along the player's own up.
            float w[3];
            for (int k = 0; k < 3; k++) w[k] = v.pose.pos[k] + v.pose.up[k] * 210.0f;
            const float* m = cam.view;
            float vx = m[0] * w[0] + m[1] * w[1] + m[2] * w[2] + m[3];
            float vy = m[4] * w[0] + m[5] * w[1] + m[6] * w[2] + m[7];
            float vz = m[8] * w[0] + m[9] * w[1] + m[10] * w[2] + m[11];
            if (vz > -50.0f) continue;  // behind the camera
            NameTag& tag = out.tags[out.count++];
            tag.x = vx / -vz / (t * aspect);
            tag.y = vy / -vz / t;
            tag.tint = v.tint;
            snprintf(tag.name, sizeof(tag.name), "%s", v.name);
        }
    }
    sTags = out;
}

// Player list (P3-04): where each remote player is, for the page's Follow
// buttons.  Stage names come from the game's galaxy table.
extern "C" const char* port_net_stage_name(uint32_t hash);  // GameDataFunction.cpp
extern "C" void port_net_request_follow(int slot);
struct RemoteEntry {
    int32_t slot;
    int32_t scenario;
    uint32_t sameStage;
    char name[36];
    char stage[48];
};
struct RemoteList {
    uint32_t count;
    RemoteEntry e[net::kMaxRemotes];
};
static RemoteList sRemotes;
extern "C" EMSCRIPTEN_KEEPALIVE RemoteList* web_remote_list(void) {
    RemoteList out{};
    for (int i = 0; i < net::kMaxRemotes && net::netEnabled(); i++) {
        const net::RemoteView& v = net::netRemote(i);
        if (!v.present) continue;
        RemoteEntry& r = out.e[out.count++];
        r.slot = i;
        r.scenario = v.pose.scenario;
        r.sameStage = v.visible ? 1 : 0;
        snprintf(r.name, sizeof(r.name), "%s", v.name);
        const char* stage = v.pose.stageHash ? port_net_stage_name(v.pose.stageHash) : nullptr;
        snprintf(r.stage, sizeof(r.stage), "%s", stage ? stage : "");
    }
    sRemotes = out;
    return &sRemotes;
}
extern "C" EMSCRIPTEN_KEEPALIVE void web_follow(int slot) { port_net_request_follow(slot); }

unsigned long long web_present_frame_number() { return gpu::renderer().frameNumber(); }
