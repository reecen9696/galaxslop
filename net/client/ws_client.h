// Minimal WebSocket client for the native build (RFC 6455, binary frames
// only, ws:// only).  The browser build talks to the relay through the
// page's own WebSocket instead (P2-03 web half); both sit behind the same
// net::Transport interface.
#pragma once

#include <stdint.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace net {

class Transport {
public:
    virtual ~Transport() = default;
    virtual bool isOpen() const = 0;
    virtual void send(const uint8_t* data, size_t len) = 0;
    // Pops one received binary message; false if none is waiting.
    virtual bool poll(std::vector<uint8_t>& out) = 0;
};

class WsClient : public Transport {
public:
    ~WsClient() override;
    // Connects to ws://host:port/path and starts the receive thread.
    // Blocking; call from a thread that may wait (not the game's frame).
    bool connect(const std::string& url, std::string* error);
    bool isOpen() const override { return mOpen.load(); }
    void send(const uint8_t* data, size_t len) override;
    bool poll(std::vector<uint8_t>& out) override;
    void close();

private:
    void recvLoop();
    void sendFrame(uint8_t opcode, const uint8_t* data, size_t len);
    bool readExact(uint8_t* p, size_t n);

    int mFd = -1;
    std::atomic<bool> mOpen{false};
    std::thread mThread;
    std::mutex mSendLock;
    std::mutex mInLock;
    std::deque<std::vector<uint8_t>> mInbox;
};

// "ws://host:port/path" -> parts; false if malformed or not ws://.
bool parseWsUrl(const std::string& url, std::string* host, int* port, std::string* path);

}  // namespace net
