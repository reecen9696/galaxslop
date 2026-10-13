// WebAssembly launcher (P0-06).  Two builds share it:
//
//   SMG_WEB_NODE=ON  bring-up in Node: the converted disc read straight from
//                    the host file system (NODERAWFS), no window, null GPU
//                    backend, scripted input.  Proves the game boots as wasm.
//                      node build/web/smg.js --game ~/smg/game [--seconds N]
//   SMG_WEB_NODE=OFF the browser build (P0-08): WebGL2 on the canvas; the game
//                    files come from the dev server through a WasmFS fetch
//                    backend (web/dev-server.mjs), listed by manifest.json.
//                    OPFS storage with an import screen replaces that (P0-07).
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

#ifndef SMG_WEB_NODE
#include <emscripten/wasmfs.h>

#include <vector>

#include "present_web.h"

// Mounts the dev server's game folder at /game: a WasmFS fetch backend reads
// each file with range requests on first use.  It cannot list directories,
// so every file named in manifest.json (tools/cook/manifest.py) is created
// up front.
static bool mountGameOverFetch(const char* mountPoint) {
    // Whole files (the largest is 31 MB): with 1 MB chunks, reads that span a
    // chunk boundary came back corrupted (Emscripten 6.0.12's fetch backend),
    // which crashed loading Honeybee Kingdom's collision.
    backend_t fetch = wasmfs_create_fetch_backend("game", 32 << 20);
    if (wasmfs_create_directory(mountPoint, 0777, fetch) != 0) {
        port_log("web: cannot mount %s", mountPoint);
        return false;
    }
    std::string manifestPath = std::string(mountPoint) + "/manifest.json";
    wasmfs_create_file(manifestPath.c_str(), 0444, fetch);
    FILE* f = fopen(manifestPath.c_str(), "rb");
    if (!f) {
        port_log("web: no manifest.json on the server (cook with tools/cook/cook.py)");
        return false;
    }
    std::string json;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) json.append(buf, n);
    fclose(f);
    // Every "path": "..." entry; the manifest is machine-written, so a plain
    // scan is enough.
    int files = 0;
    for (size_t at = json.find("\"path\""); at != std::string::npos; at = json.find("\"path\"", at + 6)) {
        size_t q1 = json.find('"', json.find(':', at) + 1);
        size_t q2 = json.find('"', q1 + 1);
        std::string rel = json.substr(q1 + 1, q2 - q1 - 1);
        std::string full = std::string(mountPoint) + "/" + rel;
        for (size_t slash = full.find('/', strlen(mountPoint) + 1); slash != std::string::npos; slash = full.find('/', slash + 1)) {
            mkdir(full.substr(0, slash).c_str(), 0777);  // inside the fetch backend
        }
        wasmfs_create_file(full.c_str(), 0444, fetch);
        files++;
    }
    port_log("web: %d game files available from the server", files);
    return files > 0;
}

// Origin-private file system (P0-07): the page copies the user's cooked game
// folder to OPFS /smg/game once (web/app.js); saves live in /smg/save, so
// they survive reloads.  WasmFS reads OPFS through sync access handles on
// its own worker.
static bool mountOpfs() {
    static bool mounted = false;
    if (mounted) {
        return true;
    }
    backend_t opfs = wasmfs_create_opfs_backend();
    if (wasmfs_create_directory("/opfs", 0777, opfs) != 0) {
        port_log("web: cannot mount OPFS");
        return false;
    }
    mkdir("/opfs/smg", 0777);
    mounted = true;
    return true;
}
#endif

extern "C" void port_boot(const char* dataRoot, const char* saveRoot);
extern "C" void port_input_set(int chan, const PortPadState* pad);
extern "C" int port_log_to_stderr;
extern "C" void port_gx_use_null_backend(void);
extern "C" void port_debug_dump_threads(void);

static long long nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

#ifndef SMG_WEB_NODE
static const PortInputEvent* sEvents;
static int sEventCount;
static long long sStart;

static void browserFrame() {
    PortPadState pad;
    web_read_input(&pad);  // the script, if any, adds its presses on top
    portApplyInputScript(sEvents, sEventCount, (int)(nowMs() - sStart), &pad);
    port_input_set(0, &pad);
    web_present_frame();
    // Stall watch: no new game frame for ~5 s dumps the OS threads once.
    static unsigned long long sLastFrame = 0;
    static unsigned sSame = 0;
    static bool sDumped = false;
    unsigned long long f = web_present_frame_number();
    sSame = f == sLastFrame ? sSame + 1 : 0;
    sLastFrame = f;
    if (sSame == 300 && !sDumped) {
        sDumped = true;
        port_log("web: no new game frame for 300 display frames (stuck at %llu)", f);
        port_debug_dump_threads();
    }
    static unsigned sCalls = 0;
    if ((++sCalls % 600) == 0) {
        port_log("web: %u display frames, renderer at game frame %llu", sCalls, web_present_frame_number());
        static const char* threads = getenv("PETARI_THREADS");
        if (threads && *threads) {
            port_debug_dump_threads();  // every emulated OS thread and where it waits
        }
    }
}
#endif

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
#ifndef SMG_WEB_NODE
    // web/app.js passes --game /opfs/smg/game once the folder is imported,
    // or nothing for the dev server's copy (?source=server).
    bool opfs = game.rfind("/opfs/", 0) == 0;
    if (game.empty()) {
        game = "/game";
        if (!mountGameOverFetch(game.c_str())) {
            return 1;
        }
    }
    if (save.empty()) {
        // Saves persist in OPFS whatever the game files come from.
        save = mountOpfs() ? "/opfs/smg/save" : "/save";
    }
    if (opfs && !mountOpfs()) {
        return 1;
    }
    seconds = 0;  // the page runs until closed
#endif
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
    if (const char* relay = getenv("SMG_RELAY")) {
        const char* room = getenv("SMG_ROOM");
        const char* name = getenv("SMG_NAME");
        fprintf(stderr, "smg-web: co-op via %s, room %s, as %s\n", relay, room && *room ? room : "(new)", name ? name : "Player");
    }
    port_boot(game.c_str(), save.c_str());

#ifndef SMG_WEB_NODE
    if (!web_present_init(2)) {
        return 1;
    }
    // Browser: one rAF-driven callback per display frame feeds input and
    // presents; the game keeps running on its own threads.
    sEvents = events;
    sEventCount = eventCount;
    sStart = start;
    // requestAnimationFrame pauses in hidden tabs; PETARI_TIMER=1 paces with
    // setTimeout instead (automated runs in background tabs).
    const char* timer = getenv("PETARI_TIMER");
    emscripten_set_main_loop(browserFrame, timer && *timer == '1' ? 60 : 0, 0);
    if (timer && *timer == '1') {
        emscripten_set_main_loop_timing(EM_TIMING_SETTIMEOUT, 16);
    }
    emscripten_exit_with_live_runtime();  // never returns
#endif
    while (seconds == 0 || nowMs() - start < (long long)seconds * 1000) {
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
