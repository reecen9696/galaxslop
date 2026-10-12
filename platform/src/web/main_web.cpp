// WebAssembly launcher (P0-06).  Two builds share it:
//
//   SMG_WEB_NODE=ON  bring-up in Node: the converted disc read straight from
//                    the host file system (NODERAWFS), no window, null GPU
//                    backend, scripted input.  Proves the game boots as wasm.
//   (default)        the browser build (P0-08): canvas, WebGL2, OPFS.  TODO.
//
//   node build/web/smg.js --game ~/smg/game [--save <dir>] [--seconds N]
//
// main() runs on a worker thread (PROXY_TO_PTHREAD), so it may block; the
// game itself runs on its emulated OS threads like on the desktop.
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <string>

#include "port/input.h"
#include "port/input_script.h"
#include "port/port.h"

extern "C" void port_boot(const char* dataRoot, const char* saveRoot);
extern "C" void port_input_set(int chan, const PortPadState* pad);
extern "C" int port_log_to_stderr;
extern "C" void port_gx_use_null_backend(void);

static long long nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main(int argc, char** argv) {
    std::string game, save;
    int seconds = 60;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--game" && i + 1 < argc) {
            game = argv[++i];
        } else if (a == "--save" && i + 1 < argc) {
            save = argv[++i];
        } else if (a == "--seconds" && i + 1 < argc) {
            seconds = atoi(argv[++i]);
        }
    }
    if (game.empty()) {
        fprintf(stderr, "usage: node smg.js --game <cooked dir> [--save <dir>] [--seconds N]\n");
        return 2;
    }
    if (save.empty()) {
        save = game + "/../save-web";
    }
    mkdir(save.c_str(), 0755);
    port_log_to_stderr = 1;

    const char* script = getenv("PETARI_INPUT");
    static PortInputEvent events[1024];
    int eventCount = script && *script ? portParseInputScript(script, events, 1024) : 0;
    PortPadState pad;
    memset(&pad, 0, sizeof(pad));
    pad.connected = 1;
    pad.pointerDist = 2.0f;
    port_input_set(0, &pad);

    long long start = nowMs();
    char startText[32];
    snprintf(startText, sizeof(startText), "%lld", start);
    setenv("PETARI_T0_MS", startText, 1);
    fprintf(stderr, "smg-web: booting %s (save %s)\n", game.c_str(), save.c_str());
    port_boot(game.c_str(), save.c_str());

    while (nowMs() - start < (long long)seconds * 1000) {
        memset(&pad, 0, sizeof(pad));
        pad.connected = 1;
        pad.pointerDist = 2.0f;
        portApplyInputScript(events, eventCount, (int)(nowMs() - start), &pad);
        port_input_set(0, &pad);
        usleep(5000);
    }
    fprintf(stderr, "smg-web: %d s elapsed, exiting\n", seconds);
    fflush(stderr);
    emscripten_force_exit(0);  // _exit() from the proxied main thread leaves Node running
    return 0;
}
