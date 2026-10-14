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
#include <unordered_map>
#include <unordered_set>

#include "../gx/gl_renderer.h"
#include "net/client/net_client.h"
#include "net/puppet/puppet_tint.h"
#include "port/board.h"
#include "port/port.h"
#include "port/talk.h"

namespace {

// 640 at 16:9; wider for 21:9 (port_screen_widen), the same pixel density.
int sEfbW = 640;
const int kEfbH = 456;
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

// Rebindable keys (P5-08): each action holds the KeyboardEvent.code values
// that press it.  The page's Settings sends the player's layout as
// "action=Code,Code;..." (SMG_KEYS at boot, web_keys_buffer + web_keys_commit
// when it changes); actions it leaves out keep their default keys.
enum Action {
    aUp, aDown, aLeft, aRight, aA, aB, aZ, aC, aSpin, aPlus, aMinus, aOne, aTwo, aHome,
    aPadUp, aPadDown, aPadLeft, aPadRight, aTalk, kActionCount,
};
const char* const kActionNames[kActionCount] = {
    "up", "down", "left", "right", "a", "b", "z", "c", "spin", "plus", "minus", "one", "two", "home",
    "dup", "ddown", "dleft", "dright", "talk",
};
const char* const kDefaultKeys =
    "up=KeyW;down=KeyS;left=KeyA;right=KeyD;a=Space;b=KeyK;z=KeyQ;c=KeyC;spin=KeyJ,ShiftLeft;plus=Enter;"
    "minus=Backspace;one=Digit1;two=Digit2;home=KeyH;dup=ArrowUp;ddown=ArrowDown;dleft=ArrowLeft;"
    "dright=ArrowRight;talk=KeyE";
std::unordered_map<std::string, uint32_t> sBinds;  // code -> bit per action
char sKeysText[1024];                               // written by the page
std::atomic<int> sKeysVersion{0};
int sKeysApplied = -1;

void parseKeys(const char* text) {
    std::string t(text);
    size_t p = 0;
    while (p < t.size()) {
        size_t end = t.find(';', p);
        if (end == std::string::npos) end = t.size();
        std::string item = t.substr(p, end - p);
        p = end + 1;
        size_t eq = item.find('=');
        if (eq == std::string::npos) continue;
        std::string name = item.substr(0, eq);
        int action = -1;
        for (int i = 0; i < kActionCount; i++) {
            if (name == kActionNames[i]) action = i;
        }
        if (action < 0) continue;
        for (auto& b : sBinds) b.second &= ~(1u << action);  // the page's list replaces the default
        size_t q = eq + 1;
        while (q <= item.size()) {
            size_t comma = item.find(',', q);
            if (comma == std::string::npos) comma = item.size();
            std::string code = item.substr(q, comma - q);
            if (!code.empty()) sBinds[code] |= 1u << action;
            q = comma + 1;
        }
    }
}

void applyKeys() {
    int v = sKeysVersion.load();
    if (v == sKeysApplied) return;
    sKeysApplied = v;
    sBinds.clear();
    parseKeys(kDefaultKeys);
    if (v == 0) {
        if (const char* env = getenv("SMG_KEYS")) parseKeys(env);
    } else {
        sKeysText[sizeof(sKeysText) - 1] = 0;
        parseKeys(sKeysText);
    }
    sKeys.clear();  // a key held across the change would stay pressed
}

bool isGameKey(const char* code) {
    applyKeys();
    auto it = sBinds.find(code);
    return it != sBinds.end() && it->second != 0;
}

bool onKey(int type, const EmscriptenKeyboardEvent* e, void*) {
    if (!isGameKey(e->code)) {
        return false;  // leave browser shortcuts alone
    }
    if (type == EMSCRIPTEN_EVENT_KEYDOWN) {
        if ((sBinds[e->code] & (1u << aTalk)) && !e->repeat) {
            port_talk_note_press();  // talk (port/talk.h)
        }
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

// Whether any key bound to the action is down.
bool held(Action action) {
    for (const std::string& code : sKeys) {
        auto it = sBinds.find(code);
        if (it != sBinds.end() && (it->second & (1u << action))) return true;
    }
    return false;
}

}  // namespace

// The thread that receives html5 and websocket callbacks (net/client/ws_web.cpp).
extern "C" pthread_t web_event_thread(void) { return sEventThread; }

static void applyScale();
static void recordTiming(double presentMs);
static std::atomic<int> sAutoMax{0};  // auto render scale: highest scale, 0 = off (see recordTiming)
static int sHold = 0;                  // frames to keep the previous picture for
static void updateNameTags();
static void loadShaderKeys();
static void saveShaderKeys();

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
    sEfbW = (int)(320.0f * port_screen_widen() + 0.5f) * 2;
    // Holding A to skip cutscenes and dialogue (cutscene_skip.cpp) is off
    // unless Settings turns it on: on the Wii a held A only speeds up text.
    const char* skip = getenv("SMG_SKIP_HOLD");
    port_skip_set_hold_seconds(skip ? (float)atof(skip) : 0.0f);
    int s = scale > 0 ? scale : 2;
    if (const char* env = getenv("SMG_SCALE")) {
        // "auto" or "auto:N": start at N (default 2) and adapt (see recordTiming).
        if (strncmp(env, "auto", 4) == 0) {
            int max = env[4] == ':' ? atoi(env + 5) : 2;
            if (max < 1 || max > 4) max = 2;
            s = max;
            sAutoMax.store(max);
        } else {
            int v = atoi(env);
            if (v >= 1 && v <= 4) s = v;
        }
    }
    sEfb = gpu::renderer().createTarget(sEfbW * s, kEfbH * s);
    emscripten_set_canvas_element_size("#canvas", sEfbW * s, kEfbH * s);
    port_log("present: WebGL2 ready, EFB %dx%d", sEfb.width, sEfb.height);
    loadShaderKeys();

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
    double t1 = emscripten_get_now();
    r.render(sEfb, nullptr, nullptr);
    static const bool perfLog = getenv("PETARI_PERFLOG") != nullptr;
    double t2 = emscripten_get_now();
    if (perfLog && t2 - t0 > 12) {
        port_log("web: slow present %.1f ms (update %.1f, render %.1f)", t2 - t0, t1 - t0, t2 - t1);
    }
    // Keep the last complete picture on screen rather than show one with
    // draws missing (a program still compiling: a skipped full-screen pass
    // turned the whole frame black) or a just-recreated target.  Not
    // touching the canvas leaves its previous frame up.  At most ~0.5 s.
    static int sHeld = 0;
    bool incomplete = r.lastSkippedDraws() > 0 || sHold > 0;
    if (sHold > 0) sHold--;
    if (incomplete && sHeld < 30) {
        sHeld++;
        recordTiming(emscripten_get_now() - t0);
        updateNameTags();
        return;
    }
    sHeld = 0;
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
    static int sFrames = 0;
    if (++sFrames % 300 == 0) saveShaderKeys();  // every ~5 s
}

// Touch controls (web/touch.js): the page's main thread writes its stick,
// buttons and finger pointer straight into this struct.  `pressed` collects
// presses since the last frame (Atomics.or) so a tap shorter than a frame
// still lands; `rumbleFrames` counts frames the game ran the remote's motor
// (the page turns them into haptics and zeroes it).
struct TouchPad {
    float stickX, stickY;      // -1..1, right / up positive
    float pointerX, pointerY;  // -1..1 over the canvas, like the mouse
    uint32_t held, pressed;    // Wii remote bits, plus kTouchSpin
    int32_t active, pointerValid;
    uint32_t rumbleFrames;
};
static TouchPad sTouch;
static const uint32_t kTouchSpin = 0x10000;
extern "C" EMSCRIPTEN_KEEPALIVE TouchPad* web_touch(void) { return &sTouch; }

// Gamepad (standard mapping) on top of keyboard/mouse, per BUILD_SPEC
// section 7.  The right stick moves a virtual pointer; once it moves, the
// pointer follows the stick until the mouse moves or a finger aims again.
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
                sCursorX = sPointerIn ? sPointerX : sTouch.pointerValid ? sTouch.pointerX : 0.0f;
                sCursorY = sPointerIn ? sPointerY : sTouch.pointerValid ? sTouch.pointerY : 0.0f;
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

static void readTouch(PortPadState* pad) {
    if (port_input_rumble(0)) __atomic_fetch_add(&sTouch.rumbleFrames, 1, __ATOMIC_RELAXED);
    if (!__atomic_load_n(&sTouch.active, __ATOMIC_RELAXED)) return;
    uint32_t bits = __atomic_load_n(&sTouch.held, __ATOMIC_RELAXED) | __atomic_exchange_n(&sTouch.pressed, 0, __ATOMIC_RELAXED);
    float sx = sTouch.stickX, sy = sTouch.stickY;
    if (sx != 0 || sy != 0) {
        pad->stickX = sx;
        pad->stickY = sy;
    }
    pad->buttons |= bits & 0xffff;
    // Spin: a shake, as with the keyboard; a tap shakes for a few frames.
    static int sSpin = 0;
    if (bits & kTouchSpin) sSpin = 6;
    if (sSpin > 0) {
        sSpin--;
        static unsigned sFlip = 0;
        float a = (++sFlip & 1) ? 2.5f : -2.5f;
        pad->accX = a;
        pad->accY = a;
    }
    // The finger's pointer stays where it was lifted, so it only takes over
    // from the gamepad's right stick when a finger aims anew (moves it), as
    // the mouse does.
    static float sLastX = 0, sLastY = 0;
    float px = sTouch.pointerX, py = sTouch.pointerY;
    if (px != sLastX || py != sLastY) {
        sLastX = px;
        sLastY = py;
        sCursorActive = false;
    }
    if (sTouch.pointerValid && !sCursorActive) {
        pad->pointerValid = 1;
        pad->pointerX = px;
        pad->pointerY = py;
    }
}

// Keyboard/mouse -> one Wii remote + Nunchuk, the desktop mapping
// (docs/BUILDING.md "Controls").
void web_read_input(PortPadState* pad) {
    memset(pad, 0, sizeof(*pad));
    pad->connected = 1;
    pad->pointerDist = 2.0f;
    applyKeys();
    pad->stickX = (held(aRight) ? 1.0f : 0.0f) - (held(aLeft) ? 1.0f : 0.0f);
    pad->stickY = (held(aUp) ? 1.0f : 0.0f) - (held(aDown) ? 1.0f : 0.0f);
    if (held(aA)) pad->buttons |= kA;
    if (held(aB) || sMouseR) pad->buttons |= kB;
    // Z is not Ctrl by default: Ctrl+W/D/S with the movement keys are the
    // browser's (close the tab, bookmark, save), and a page cannot stop Ctrl+W.
    if (held(aZ)) pad->buttons |= kZ;
    if (held(aC)) pad->buttons |= kC;
    if (held(aPlus)) pad->buttons |= kPlus;  // Esc is the page's: it shows the cursor
    if (held(aMinus)) pad->buttons |= kMinus;
    if (held(aOne)) pad->buttons |= kOne;
    if (held(aTwo)) pad->buttons |= kTwo;
    if (held(aHome)) pad->buttons |= kHome;
    if (held(aPadUp)) pad->buttons |= kUp;
    if (held(aPadDown)) pad->buttons |= kDown;
    if (held(aPadLeft)) pad->buttons |= kLeft;
    if (held(aPadRight)) pad->buttons |= kRight;
    // Spin: a shake of the remote, alternating acceleration each frame.
    if (held(aSpin) || sMouseL) {
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
    readTouch(pad);
}

// Settings > Controls: the page writes the layout here, then commits it; the
// event thread applies it at its next key or frame.
extern "C" EMSCRIPTEN_KEEPALIVE char* web_keys_buffer(void) { return sKeysText; }
extern "C" EMSCRIPTEN_KEEPALIVE int web_keys_buffer_size(void) { return (int)sizeof(sKeysText); }
extern "C" EMSCRIPTEN_KEEPALIVE void web_keys_commit(void) { sKeysVersion.fetch_add(1); }

// Render scale (P1-07): the page asks for a new EFB size (settings);
// applied between frames on this thread, which owns the GL context.
static std::atomic<int> sWantScale{0};
// Auto scale: 0 = off (fixed scale), else the highest scale allowed.  Frames
// that come late (the GPU is behind: a screen full of blended particles at
// 2x) step the resolution down; steady frames step it back up.
extern "C" EMSCRIPTEN_KEEPALIVE void web_request_scale(int scale) {
    sAutoMax.store(0);
    sWantScale.store(scale);
}
extern "C" EMSCRIPTEN_KEEPALIVE void web_set_auto_scale(int maxScale) { sAutoMax.store(maxScale); }
// Settings' "Hold A to skip": seconds, 0 = off.  Read on the game thread at
// its next update; a float store is as good as atomic here.
extern "C" EMSCRIPTEN_KEEPALIVE void web_set_skip_hold(float seconds) { port_skip_set_hold_seconds(seconds); }

static void applyScale() {
    int want = sWantScale.exchange(0);
    if (want < 1 || want > 4 || sEfb.width == sEfbW * want) {
        return;
    }
    gpu::renderer().destroyTarget(sEfb);
    sEfb = gpu::renderer().createTarget(sEfbW * want, kEfbH * want);
    // The canvas only grows (a resize clears it, which showed as a black
    // flash on every auto switch); the blit scales the EFB to it.
    int cw = 0, ch = 0;
    emscripten_get_canvas_element_size("#canvas", &cw, &ch);
    if (sEfbW * want > cw) emscripten_set_canvas_element_size("#canvas", sEfbW * want, kEfbH * want);
    sHold = 2;  // the new target's first frames may lack the EFB copies effects read
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
    // Auto scale: three late seconds in a row step down, twenty steady
    // seconds step up (late: p95 interval over 19 ms, a missed vsync in ~20).
    // Only seconds in which the game itself kept up count as late: during a
    // load or a CPU stall a lower resolution would not help.
    static int sLate = 0, sSteady = 0;
    static uint32_t sLastGameFrame = 0;
    uint32_t gameDelta = sStats.gameFrame - sLastGameFrame;
    sLastGameFrame = sStats.gameFrame;
    int autoMax = sAutoMax.load();
    if (autoMax > 0) {
        int cur = sEfb.width / sEfbW;
        sLate = (sStats.intervalP95 > 19.0f && gameDelta >= 55) ? sLate + 1 : 0;
        sSteady = sStats.intervalP95 < 17.5f ? sSteady + 1 : 0;
        if (sLate >= 3 && cur > 1) {
            sWantScale.store(cur - 1);
            port_log("present: auto scale down to %dx (frame interval p95 %.1f ms)", cur - 1, sStats.intervalP95);
            sLate = sSteady = 0;
        } else if (sSteady >= 20 && cur < autoMax) {
            sWantScale.store(cur + 1);
            port_log("present: auto scale up to %dx", cur + 1);
            sLate = sSteady = 0;
        } else if (cur > autoMax) {
            sWantScale.store(autoMax);
        }
    }
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
    uint32_t tint;  // net::kTint*, resolved (never auto)
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
            tag.tint = net::puppetTint(v.tint, v.id);
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

// Leaderboard (P5-07): the local player's pickups per run (port/board.h).
extern "C" EMSCRIPTEN_KEEPALIVE PortBoard* web_board(void) { return port_board(); }

// Shader warm-up (P1-06): every program key the renderer builds is saved to
// OPFS; the next session compiles them in the background from the loading
// screen on, so effects seen before don't stall the frame when they appear.
static const char* const kShaderKeysPath = "/opfs/smg/shaders.bin";
static std::unordered_set<std::string> sSavedKeys;
static size_t sKeySize = 0;

static void loadShaderKeys() {
    FILE* f = fopen(kShaderKeysPath, "rb");
    if (!f) return;
    std::string all;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
    fclose(f);
    // The file starts with the key size, so a key layout change starts over.
    if (all.size() < 4) return;
    uint32_t size;
    memcpy(&size, all.data(), 4);
    std::vector<uint8_t> probe;
    if (size == 0 || (all.size() - 4) % size != 0) return;
    sKeySize = size;
    for (size_t at = 4; at + size <= all.size(); at += size) sSavedKeys.insert(all.substr(at, size));
    std::string keys;
    for (const std::string& k : sSavedKeys) keys += k;
    gpu::renderer().queueWarmUp(keys.data(), keys.size());
    port_log("present: warming up %zu shader programs from earlier sessions", sSavedKeys.size());
}

static void saveShaderKeys() {
    std::vector<uint8_t> fresh;
    size_t n = gpu::renderer().takeNewShaderKeys(fresh);
    if (n == 0) return;
    size_t size = fresh.size() / n;
    std::string out;
    for (size_t i = 0; i < n; i++) {
        std::string k((const char*)fresh.data() + i * size, size);
        if (sSavedKeys.insert(k).second) out += k;
    }
    if (out.empty()) return;
    bool start = sKeySize != size;
    FILE* f = fopen(kShaderKeysPath, start ? "wb" : "ab");
    if (!f) return;
    if (start) {
        uint32_t s32 = (uint32_t)size;
        fwrite(&s32, 4, 1, f);
        sKeySize = size;
    }
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
}

unsigned long long web_present_frame_number() { return gpu::renderer().frameNumber(); }
