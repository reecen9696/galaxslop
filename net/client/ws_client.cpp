// WsClient: see the header.  POSIX sockets; desktop builds only.
#include "ws_client.h"

#if !defined(__EMSCRIPTEN__)

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <random>

#include "port/heap_routing.h"

namespace net {

namespace {

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? (uint32_t)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        s += kB64[(v >> 18) & 63];
        s += kB64[(v >> 12) & 63];
        s += i + 1 < n ? kB64[(v >> 6) & 63] : '=';
        s += i + 2 < n ? kB64[v & 63] : '=';
    }
    return s;
}

}  // namespace

bool parseWsUrl(const std::string& url, std::string* host, int* port, std::string* path) {
    const std::string scheme = "ws://";
    if (url.compare(0, scheme.size(), scheme) != 0) return false;
    std::string rest = url.substr(scheme.size());
    size_t slash = rest.find('/');
    std::string hostport = rest.substr(0, slash);
    *path = slash == std::string::npos ? "/" : rest.substr(slash);
    size_t colon = hostport.rfind(':');
    if (colon == std::string::npos) {
        *host = hostport;
        *port = 80;
    } else {
        *host = hostport.substr(0, colon);
        *port = atoi(hostport.c_str() + colon + 1);
    }
    return !host->empty() && *port > 0 && *port < 65536;
}

WsClient::~WsClient() { close(); }

bool WsClient::connect(const std::string& url, std::string* error) {
    std::string host, path;
    int port = 0;
    if (!parseWsUrl(url, &host, &port, &path)) {
        *error = "not a ws://host:port URL: " + url;
        return false;
    }
    addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portStr[16];
    snprintf(portStr, sizeof(portStr), "%d", port);
    if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || res == nullptr) {
        *error = "cannot resolve " + host;
        return false;
    }
    for (addrinfo* a = res; a != nullptr; a = a->ai_next) {
        int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, a->ai_addr, a->ai_addrlen) == 0) {
            mFd = fd;
            break;
        }
        ::close(fd);
    }
    freeaddrinfo(res);
    if (mFd < 0) {
        *error = "cannot connect to " + host + ":" + portStr;
        return false;
    }
    int one = 1;
    setsockopt(mFd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));  // small frames at 30 Hz

    uint8_t keyBytes[16];
    std::random_device rd;
    for (uint8_t& b : keyBytes) b = (uint8_t)rd();
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + ":" + portStr +
                      "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " +
                      base64(keyBytes, sizeof(keyBytes)) + "\r\n\r\n";
    if (::send(mFd, req.data(), req.size(), 0) != (ssize_t)req.size()) {
        *error = "handshake send failed";
        ::close(mFd);
        mFd = -1;
        return false;
    }
    // Read the response headers byte by byte up to the blank line.
    std::string resp;
    char ch;
    while (resp.size() < 4096 && recv(mFd, &ch, 1, 0) == 1) {
        resp += ch;
        if (resp.size() >= 4 && resp.compare(resp.size() - 4, 4, "\r\n\r\n") == 0) break;
    }
    if (resp.compare(0, 12, "HTTP/1.1 101") != 0) {
        *error = "relay refused the WebSocket upgrade: " + resp.substr(0, resp.find('\r'));
        ::close(mFd);
        mFd = -1;
        return false;
    }
    mOpen = true;
    PortHostAllocScope hostAlloc;  // a host thread: its allocations are not the game's
    mThread = std::thread([this] {
        PortHostAllocScope scope;
        recvLoop();
    });
    return true;
}

bool WsClient::readExact(uint8_t* p, size_t n) {
    while (n > 0) {
        ssize_t got = recv(mFd, p, n, 0);
        if (got <= 0) return false;
        p += got;
        n -= (size_t)got;
    }
    return true;
}

void WsClient::recvLoop() {
    std::vector<uint8_t> message;
    while (mOpen.load()) {
        uint8_t hdr[2];
        if (!readExact(hdr, 2)) break;
        bool fin = hdr[0] & 0x80;
        uint8_t opcode = hdr[0] & 0x0F;
        uint64_t len = hdr[1] & 0x7F;
        if (len == 126) {
            uint8_t e[2];
            if (!readExact(e, 2)) break;
            len = (uint64_t)e[0] << 8 | e[1];
        } else if (len == 127) {
            uint8_t e[8];
            if (!readExact(e, 8)) break;
            len = 0;
            for (uint8_t b : e) len = len << 8 | b;
        }
        if (len > (1u << 20)) break;  // the relay never sends anything this big
        std::vector<uint8_t> payload((size_t)len);
        if (len && !readExact(payload.data(), (size_t)len)) break;  // server frames are unmasked
        if (opcode == 0x8) break;                                    // close
        if (opcode == 0x9) {                                         // ping -> pong
            sendFrame(0xA, payload.data(), payload.size());
            continue;
        }
        if (opcode == 0xA) continue;  // pong
        if (opcode == 0x2 || opcode == 0x0) {
            message.insert(message.end(), payload.begin(), payload.end());
            if (fin) {
                std::lock_guard<std::mutex> lock(mInLock);
                mInbox.push_back(std::move(message));
                message.clear();
            }
        }
    }
    mOpen = false;
}

void WsClient::sendFrame(uint8_t opcode, const uint8_t* data, size_t len) {
    std::vector<uint8_t> f;
    f.reserve(len + 14);
    f.push_back((uint8_t)(0x80 | opcode));
    if (len < 126) {
        f.push_back((uint8_t)(0x80 | len));
    } else if (len < 65536) {
        f.push_back(0x80 | 126);
        f.push_back((uint8_t)(len >> 8));
        f.push_back((uint8_t)len);
    } else {
        f.push_back(0x80 | 127);
        for (int i = 7; i >= 0; i--) f.push_back((uint8_t)((uint64_t)len >> (i * 8)));
    }
    static thread_local std::minstd_rand rng(std::random_device{}());
    uint8_t mask[4];
    for (uint8_t& m : mask) m = (uint8_t)rng();
    f.insert(f.end(), mask, mask + 4);  // clients must mask (RFC 6455 5.3)
    for (size_t i = 0; i < len; i++) f.push_back(data[i] ^ mask[i & 3]);
    std::lock_guard<std::mutex> lock(mSendLock);
    if (mFd >= 0) {
        ::send(mFd, f.data(), f.size(), 0);
    }
}

void WsClient::send(const uint8_t* data, size_t len) {
    if (mOpen.load()) sendFrame(0x2, data, len);
}

bool WsClient::poll(std::vector<uint8_t>& out) {
    std::lock_guard<std::mutex> lock(mInLock);
    if (mInbox.empty()) return false;
    out = std::move(mInbox.front());
    mInbox.pop_front();
    return true;
}

void WsClient::close() {
    if (mFd >= 0) {
        mOpen = false;
        shutdown(mFd, SHUT_RDWR);
        if (mThread.joinable()) mThread.join();
        ::close(mFd);
        mFd = -1;
    }
}

}  // namespace net

#endif  // !__EMSCRIPTEN__
