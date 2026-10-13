// See ws_web.h.
#ifdef __EMSCRIPTEN__
#include "net/client/ws_web.h"

#include <unistd.h>

#include "port/heap_routing.h"
#include "port/port.h"

extern "C" pthread_t web_event_thread(void);  // platform/src/web/present_web.cpp

namespace net {

WebWsClient::~WebWsClient() {
    if (mSocket > 0) {
        emscripten_websocket_close(mSocket, 1000, "bye");
        emscripten_websocket_delete(mSocket);
    }
}

bool WebWsClient::connect(const std::string& url, std::string* error) {
    EmscriptenWebSocketCreateAttributes attrs;
    emscripten_websocket_init_create_attributes(&attrs);
    attrs.url = url.c_str();
    attrs.createOnMainThread = true;  // shared by the helper, game and frame threads
    mSocket = emscripten_websocket_new(&attrs);
    if (mSocket <= 0) {
        *error = "cannot create a WebSocket for " + url;
        return false;
    }
    pthread_t events = web_event_thread();
    emscripten_websocket_set_onopen_callback_on_thread(mSocket, this, onOpen, events);
    emscripten_websocket_set_onclose_callback_on_thread(mSocket, this, onClose, events);
    emscripten_websocket_set_onerror_callback_on_thread(mSocket, this, onError, events);
    emscripten_websocket_set_onmessage_callback_on_thread(mSocket, this, onMessage, events);
    for (int i = 0; i < 1000 && !mOpen.load() && !mFailed.load(); i++) {
        usleep(10000);
    }
    if (!mOpen.load()) {
        *error = "cannot reach " + url;
        return false;
    }
    return true;
}

void WebWsClient::send(const uint8_t* data, size_t len) {
    if (mOpen.load()) {
        emscripten_websocket_send_binary(mSocket, (void*)data, (uint32_t)len);
    }
}

bool WebWsClient::poll(std::vector<uint8_t>& out) {
    PortHostAllocScope hostAlloc;  // never a game-heap safe point under mInLock
    std::lock_guard<std::mutex> lk(mInLock);
    if (mInbox.empty()) {
        return false;
    }
    out = std::move(mInbox.front());
    mInbox.pop_front();
    return true;
}

bool WebWsClient::onOpen(int, const EmscriptenWebSocketOpenEvent*, void* self) {
    ((WebWsClient*)self)->mOpen = true;
    return true;
}

bool WebWsClient::onClose(int, const EmscriptenWebSocketCloseEvent* e, void* self) {
    WebWsClient* c = (WebWsClient*)self;
    if (c->mOpen.exchange(false)) {
        port_log("net: relay closed the connection (%d)", (int)e->code);
    }
    c->mFailed = true;
    return true;
}

bool WebWsClient::onError(int, const EmscriptenWebSocketErrorEvent*, void* self) {
    ((WebWsClient*)self)->mFailed = true;
    return true;
}

bool WebWsClient::onMessage(int, const EmscriptenWebSocketMessageEvent* e, void* self) {
    if (e->isText) {
        return true;  // the relay only sends binary
    }
    WebWsClient* c = (WebWsClient*)self;
    PortHostAllocScope hostAlloc;
    std::vector<uint8_t> m(e->data, e->data + e->numBytes);
    std::lock_guard<std::mutex> lk(c->mInLock);
    c->mInbox.push_back(std::move(m));
    return true;
}

}  // namespace net
#endif
