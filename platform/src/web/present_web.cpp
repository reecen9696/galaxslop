// Browser presentation (P0-08): a WebGL2 context on the canvas, which
// Emscripten transfers to the proxied main thread as an OffscreenCanvas, and
// the same gpu::Renderer the desktop uses (port/gl.h picks GLSL ES 3.00).
// web_present_frame runs from emscripten_set_main_loop (requestAnimationFrame
// on that worker), so the browser presents each frame when the callback
// returns.  An explicit-swap loop that never returned to the event loop
// stalled on emscripten_webgl_commit_frame.
#include "present_web.h"

#include <emscripten/html5.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <unordered_set>

#include "../gx/gl_renderer.h"
#include "port/port.h"

namespace {

const int kEfbW = 640, kEfbH = 456;
EMSCRIPTEN_WEBGL_CONTEXT_HANDLE sCtx = 0;
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

bool web_present_init(int scale) {
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
}

unsigned long long web_present_frame_number() { return gpu::renderer().frameNumber(); }
