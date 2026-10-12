// Desktop presentation: an SDL3 window with a GL 4.1 core context, the
// recorded frames replayed through gpu::Renderer into an EFB-sized target,
// and that target scaled into the window.  Keyboard and mouse feed the Wii
// remote emulation.  Runs on the process's main thread (Cocoa needs that);
// the game runs on its own emulated OS threads.
#include "present.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "../gx/gl_renderer.h"
#include "../gx/gx_record.h"
#include "port/input.h"
#include "port/port.h"

#ifdef __APPLE__
#include <objc/message.h>
#include <objc/runtime.h>
#endif

extern "C" void port_headless_write_png(const char* path, const unsigned char* rgba, int w, int h);

namespace {

#ifdef __APPLE__
// Opts out of App Nap, which throttles a hidden or backgrounded app with no
// audio almost to a stop (seen: 57 fps -> 0.5 fps after ~30 s headless).  The
// game must keep running when its window is not in front, above all in co-op.
// [[NSProcessInfo processInfo] beginActivityWithOptions:reason:], via the
// Objective-C runtime so this file stays C++.
void disableAppNap() {
    const unsigned long long kUserInitiated = 0x00FFFFFFull;   // NSActivityUserInitiated
    const unsigned long long kLatencyCritical = 0xFF00000000ull;  // NSActivityLatencyCritical
    id info = ((id(*)(id, SEL))objc_msgSend)((id)objc_getClass("NSProcessInfo"), sel_registerName("processInfo"));
    id reason = ((id(*)(id, SEL, const char*))objc_msgSend)((id)objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"),
                                                            "Super Mario Galaxy is running");
    id activity = ((id(*)(id, SEL, unsigned long long, id))objc_msgSend)(info, sel_registerName("beginActivityWithOptions:reason:"),
                                                                        kUserInitiated | kLatencyCritical, reason);
    ((id(*)(id, SEL))objc_msgSend)(activity, sel_registerName("retain"));  // held for the process's life
}
#endif

// Native EFB size the recorder works in (headless_render.cpp's base size).
const int kEfbW = 640, kEfbH = 456;

SDL_Window* sWindow;
SDL_GLContext sContext;
gpu::EfbTarget sEfb;
int sScale = 2;

// WPAD button bits (revolution/wpad.h).
enum : uint32_t {
    kLeft = 0x0001, kRight = 0x0002, kDown = 0x0004, kUp = 0x0008, kPlus = 0x0010, kTwo = 0x0100, kOne = 0x0200,
    kB = 0x0400, kA = 0x0800, kMinus = 0x1000, kZ = 0x2000, kC = 0x4000, kHome = 0x8000,
};

}  // namespace

bool desktop_present_init(bool headless, int scale) {
    sScale = scale > 0 ? scale : 2;
#ifdef __APPLE__
    disableAppNap();
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        port_log("present: SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (headless) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    // SMG_WINDOW="x,y,w,h": where the window goes (two clients side by side).
    int wx = -1, wy = -1, ww = 1280, wh = 720;
    if (const char* geo = getenv("SMG_WINDOW")) {
        sscanf(geo, "%d,%d,%d,%d", &wx, &wy, &ww, &wh);
    }
    char title[96];
    snprintf(title, sizeof(title), "Super Mario Galaxy (smg-coop)%s%s", getenv("SMG_NAME") ? " - " : "", getenv("SMG_NAME") ? getenv("SMG_NAME") : "");
    sWindow = SDL_CreateWindow(title, ww, wh, flags);
    if (sWindow && wx >= 0 && wy >= 0) {
        SDL_SetWindowPosition(sWindow, wx, wy);
    }
    if (!sWindow) {
        port_log("present: SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    sContext = SDL_GL_CreateContext(sWindow);
    if (!sContext) {
        port_log("present: GL 4.1 context failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetSwapInterval(headless ? 0 : 1);
    if (!gpu::renderer().init()) {
        port_log("present: renderer init failed");
        return false;
    }
    sEfb = gpu::renderer().createTarget(kEfbW * sScale, kEfbH * sScale);
    return true;
}

// Keyboard/mouse -> one Wii remote + Nunchuk.  Mapping per BUILD_SPEC 7.
static void readInput(PortPadState* pad, bool* quit) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F12)) {
            *quit = true;
        }
    }
    const bool* k = SDL_GetKeyboardState(nullptr);
    memset(pad, 0, sizeof(*pad));
    pad->connected = 1;
    pad->pointerDist = 2.0f;
    pad->stickX = (k[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_A] ? 1.0f : 0.0f);
    pad->stickY = (k[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_S] ? 1.0f : 0.0f);
    if (k[SDL_SCANCODE_SPACE]) pad->buttons |= kA;
    if (k[SDL_SCANCODE_K]) pad->buttons |= kB;
    if (k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL]) pad->buttons |= kZ;
    if (k[SDL_SCANCODE_C]) pad->buttons |= kC;
    if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_ESCAPE]) pad->buttons |= kPlus;
    if (k[SDL_SCANCODE_BACKSPACE]) pad->buttons |= kMinus;
    if (k[SDL_SCANCODE_1]) pad->buttons |= kOne;
    if (k[SDL_SCANCODE_2]) pad->buttons |= kTwo;
    if (k[SDL_SCANCODE_H]) pad->buttons |= kHome;
    if (k[SDL_SCANCODE_UP]) pad->buttons |= kUp;
    if (k[SDL_SCANCODE_DOWN]) pad->buttons |= kDown;
    if (k[SDL_SCANCODE_LEFT]) pad->buttons |= kLeft;
    if (k[SDL_SCANCODE_RIGHT]) pad->buttons |= kRight;
    // Spin: a shake of the remote (alternating acceleration, as headless.c's
    // scripted spins do).
    if (k[SDL_SCANCODE_J] || k[SDL_SCANCODE_LSHIFT]) {
        float s = (SDL_GetTicks() / 16) & 1 ? 2.5f : -2.5f;
        pad->accX = s;
        pad->accY = s;
    }
    // Pointer: the mouse over the window.
    float mx, my;
    SDL_MouseButtonFlags mb = SDL_GetMouseState(&mx, &my);
    int ww, wh;
    SDL_GetWindowSize(sWindow, &ww, &wh);
    if (ww > 0 && wh > 0 && SDL_GetMouseFocus() == sWindow) {
        pad->pointerValid = 1;
        pad->pointerX = mx / ww * 2.0f - 1.0f;
        pad->pointerY = my / wh * 2.0f - 1.0f;
    }
    if (mb & SDL_BUTTON_LMASK) {
        float s = (SDL_GetTicks() / 16) & 1 ? 2.5f : -2.5f;
        pad->accX = s;
        pad->accY = s;
    }
    if (mb & SDL_BUTTON_RMASK) pad->buttons |= kB;
}

bool desktop_present_frame(PortPadState* pad, bool useKeyboard) {
    bool quit = false;
    PortPadState local;
    readInput(&local, &quit);
    if (useKeyboard) {
        *pad = local;
    }
    gpu::Renderer& r = gpu::renderer();
    r.update();
    r.render(sEfb, nullptr, nullptr);

    // EFB -> window, aspect kept (the EFB is stored bottom-up like GL's
    // default framebuffer, so no flip).
    int dw, dh;
    SDL_GetWindowSizeInPixels(sWindow, &dw, &dh);
    float s = (float)dw / sEfb.width < (float)dh / sEfb.height ? (float)dw / sEfb.width : (float)dh / sEfb.height;
    int w = (int)(sEfb.width * s), h = (int)(sEfb.height * s);
    int x = (dw - w) / 2, y = (dh - h) / 2;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, dw, dh);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sEfb.fbo);
    glBlitFramebuffer(0, 0, sEfb.width, sEfb.height, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    SDL_GL_SwapWindow(sWindow);
    return !quit;
}

unsigned long long desktop_present_shot(const char* path) {
    gpu::Renderer& r = gpu::renderer();
    if (!r.hasFrame()) {
        return 0;
    }
    int w = sEfb.width, h = sEfb.height;
    std::vector<unsigned char> px((size_t)w * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, sEfb.fbo);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
    port_headless_write_png(path, px.data(), w, h);
    return r.frameNumber();
}

void desktop_present_shutdown() {
    if (sContext) SDL_GL_DestroyContext(sContext);
    if (sWindow) SDL_DestroyWindow(sWindow);
    SDL_Quit();
}
