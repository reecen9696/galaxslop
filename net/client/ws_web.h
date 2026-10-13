// WebSocket transport for the browser build (P2-03 web half): the page's own
// WebSocket through Emscripten's websocket API.  Callbacks run on the
// browser-frame thread (platform/src/web), which returns to its event loop
// every display frame; the game thread only sends and polls.
#pragma once

#ifdef __EMSCRIPTEN__
#include <emscripten/websocket.h>

#include "net/client/ws_client.h"

namespace net {

class WebWsClient : public Transport {
public:
    ~WebWsClient() override;
    // Opens ws:// or wss:// and waits up to 10 s for the handshake.
    // Blocking; call from a helper thread (not the browser-frame thread).
    bool connect(const std::string& url, std::string* error);
    bool isOpen() const override { return mOpen.load(); }
    void send(const uint8_t* data, size_t len) override;
    bool poll(std::vector<uint8_t>& out) override;

private:
    static bool onOpen(int, const EmscriptenWebSocketOpenEvent*, void* self);
    static bool onClose(int, const EmscriptenWebSocketCloseEvent*, void* self);
    static bool onError(int, const EmscriptenWebSocketErrorEvent*, void* self);
    static bool onMessage(int, const EmscriptenWebSocketMessageEvent*, void* self);

    EMSCRIPTEN_WEBSOCKET_T mSocket = 0;
    std::atomic<bool> mOpen{false};
    std::atomic<bool> mFailed{false};
    std::mutex mInLock;
    std::deque<std::vector<uint8_t>> mInbox;
};

}  // namespace net
#endif
