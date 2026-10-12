// Desktop launcher (macOS, Linux): the native counterpart of
// platform/android/launcher.c and headless.c.
//
// The game stores pointers in u32 fields, so all of its code, globals, heaps
// and stacks must live below 4 GiB.  On Android the launcher loads libgame.so
// at 0x98000000 into a reserved window.  Here the game is linked into this
// executable with a preferred base of 0x98000000 (CMakeLists.txt) and a tiny
// __PAGEZERO; arm64 macOS executables are always position independent, so the
// launcher re-executes itself once with ASLR disabled to make the kernel honour
// that base.  Then it reserves the Wii window (0x80000000..0xE0010000) and
// boots the game exactly like the Android headless tool.
//
//   smg --game <cooked dir> [--save <dir>] [--headless] [--seconds N]
//       [--shots <dir>] [--shot-every <ms>] [--scale N]
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <string>

#include "port/input.h"
#include "port/input_script.h"
#include "port/port.h"
#include "present.h"

#ifdef __APPLE__
#include <mach-o/dyld.h>
extern char** environ;
#ifndef _POSIX_SPAWN_DISABLE_ASLR
#define _POSIX_SPAWN_DISABLE_ASLR 0x0100
#endif
#endif

extern "C" void port_mem_set_reserved_window(uintptr_t base, size_t size);
extern "C" void port_boot(const char* dataRoot, const char* saveRoot);
extern "C" void port_input_set(int chan, const PortPadState* pad);
extern "C" int port_log_to_stderr;

static const uintptr_t kWindowBase = 0x80000000u;
static const size_t kWindowSize = 0x60010000u;  // up to 0xE0010000 (locked cache)
static const uintptr_t kImageBase = 0x98000000u;  // CMakeLists.txt links the executable here
static const size_t kImageSize = 0x08000000u;
static const uintptr_t kImageLimit = 0x100000000ull;

static const char* envSet(const char* name) {
    const char* v = getenv(name);
    return v && *v ? v : nullptr;
}

static long long nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// A global in this image's data segment: where it lands is where the image is.
static int gImageProbe;
static uintptr_t imageAddr() { return (uintptr_t)&gImageProbe; }

// True when this image (code and globals) lies below 4 GiB.
static bool imageIsLow() { return imageAddr() < kImageLimit; }

#ifdef __APPLE__
// Runs this executable again with ASLR off; returns only on failure.
static void reexecWithoutAslr(char** argv) {
    if (envSet("SMG_NO_ASLR_REEXEC")) {
        return;  // already tried once: don't loop
    }
    setenv("SMG_NO_ASLR_REEXEC", "1", 1);
    char path[4096];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) {
        return;
    }
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETEXEC | _POSIX_SPAWN_DISABLE_ASLR);
    posix_spawn(nullptr, path, nullptr, &attr, argv, environ);
    fprintf(stderr, "smg: re-exec without ASLR failed: %s\n", strerror(errno));
}
#endif

static void usage(const char* argv0) {
    fprintf(stderr,
            "usage: %s --game <cooked dir> [--save <dir>] [--headless] [--seconds N] [--shots <dir>] [--shot-every <ms>] [--scale N]\n"
            "  --game       folder written by tools/cook/cook.py\n"
            "  --save       NAND save folder (default: <game>/../save)\n"
            "  --headless   hidden window; run N seconds (default 60) and exit 0\n"
            "  --seconds    stop after N seconds (default: 60 headless, never windowed)\n"
            "  --shots      write <dir>/frame_NNN.png every --shot-every ms (default 2000)\n"
            "  --scale      EFB size as a multiple of 640x456 (default 2)\n"
            "keys: WASD stick, Space A, K B, J/Shift/left click spin, Ctrl Z, C C, Enter +, Backspace -,\n"
            "      1/2, arrows D-pad, H Home, mouse pointer, right click B, F12 quit\n",
            argv0);
}

int main(int argc, char** argv) {
    std::string game, save, shots;
    bool headless = false;
    int seconds = -1, shotEvery = 2000, scale = 2;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--game" && i + 1 < argc) {
            game = argv[++i];
        } else if (a == "--save" && i + 1 < argc) {
            save = argv[++i];
        } else if (a == "--headless") {
            headless = true;
        } else if (a == "--seconds" && i + 1 < argc) {
            seconds = atoi(argv[++i]);
        } else if (a == "--shots" && i + 1 < argc) {
            shots = argv[++i];
        } else if (a == "--shot-every" && i + 1 < argc) {
            shotEvery = atoi(argv[++i]);
        } else if (a == "--scale" && i + 1 < argc) {
            scale = atoi(argv[++i]);
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (game.empty()) {
        usage(argv[0]);
        return 2;
    }
    if (save.empty()) {
        save = game + "/../save";
    }
    if (seconds < 0) {
        seconds = headless ? 60 : 0;  // 0: until the window closes
    }
    mkdir(save.c_str(), 0755);

    if (!imageIsLow()) {
#ifdef __APPLE__
        reexecWithoutAslr(argv);
#endif
        fprintf(stderr, "smg: the executable is mapped at %p, above 4 GiB; the game needs it below (see platform/src/native/main.cpp)\n",
                (void*)imageAddr());
        return 1;
    }

    // The window holds MEM1/MEM2 (0x80000000-0x98000000), this executable's
    // image (0x98000000-0xA0000000, mapped by the kernel), the low pool, the
    // uncached mirrors and the locked cache (0xA0000000-0xE0010000).  Reserve
    // the two halves around the image; os_mem.cpp maps into them with MAP_FIXED
    // and never touches the image slot.
    const struct { uintptr_t base, end; } halves[] = {{kWindowBase, kImageBase}, {kImageBase + kImageSize, kWindowBase + kWindowSize}};
    for (const auto& h : halves) {
        void* p = mmap((void*)h.base, h.end - h.base, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (p != (void*)h.base) {
            fprintf(stderr, "smg: cannot reserve %p-%p for the game (got %p, errno %d)\n", (void*)h.base, (void*)h.end, p, errno);
            return 1;
        }
    }
    if (imageAddr() < kImageBase || imageAddr() >= kImageBase + kImageSize) {
        fprintf(stderr, "smg: image at %p, outside its slot %p+%zx\n", (void*)imageAddr(), (void*)kImageBase, kImageSize);
        return 1;
    }
    port_mem_set_reserved_window(kWindowBase, kWindowSize);
    void* win = (void*)kWindowBase;
    port_log_to_stderr = 1;

    static PortInputEvent events[1024];
    int eventCount = envSet("PETARI_INPUT") ? portParseInputScript(envSet("PETARI_INPUT"), events, 1024) : 0;
    PortPadState pad;
    memset(&pad, 0, sizeof(pad));
    pad.connected = 1;
    pad.pointerDist = 2.0f;
    port_input_set(0, &pad);

    long long start = nowMs();
    char startText[32];
    snprintf(startText, sizeof(startText), "%lld", start);
    setenv("PETARI_T0_MS", startText, 1);
    fprintf(stderr, "smg: image at %p, window %p+%zx reserved; booting %s\n", (void*)imageAddr(), win, kWindowSize, game.c_str());
    if (!desktop_present_init(headless, scale)) {
        fprintf(stderr, "smg: no window / GL 4.1 context\n");
        return 1;
    }
    port_boot(game.c_str(), save.c_str());

    // The game runs on its emulated OS threads; this thread presents frames
    // and feeds input (the keyboard, or PETARI_INPUT's script).
    if (!shots.empty()) {
        mkdir(shots.c_str(), 0755);
    }
    long long nextShot = start + shotEvery;
    int shotIndex = 0;
    for (;;) {
        long long now = nowMs();
        if (seconds > 0 && now - start >= (long long)seconds * 1000) {
            break;
        }
        bool scripted = eventCount > 0 || headless;
        if (scripted) {
            memset(&pad, 0, sizeof(pad));
            pad.connected = 1;
            pad.pointerDist = 2.0f;
            portApplyInputScript(events, eventCount, (int)(now - start), &pad);
        }
        if (!desktop_present_frame(&pad, !scripted)) {
            break;
        }
        port_input_set(0, &pad);
        if (!shots.empty() && now >= nextShot) {
            nextShot += shotEvery;
            char path[1024];
            snprintf(path, sizeof(path), "%s/frame_%03d.png", shots.c_str(), shotIndex++);
            unsigned long long n = desktop_present_shot(path);
            fprintf(stderr, "smg: %.1f s %s (game frame %llu)\n", (now - start) / 1000.0, n ? path : "no frame yet", n);
        }
        if (headless) {
            usleep(5000);
        }
    }
    fprintf(stderr, "smg: %.0f s elapsed, exiting\n", (nowMs() - start) / 1000.0);
    _exit(0);
}
