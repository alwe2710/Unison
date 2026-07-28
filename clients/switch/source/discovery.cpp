#include "discovery.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <switch.h>

#include "finlink/discovery.h"
#include "finlink/handshake.h"

namespace {

// Minimal blocking-with-timeout HTTP/1.0 GET, identical to
// clients/3ds/source/discovery.cpp's -- not a general-purpose HTTP client,
// just enough for this protocol's /status endpoint.
std::optional<std::string> httpGet(const std::string &host, int port, const std::string &path, int timeoutMs) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return std::nullopt;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        close(fd);
        return std::nullopt;
    }

    int rc = ::connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        close(fd);
        return std::nullopt;
    }
    if (rc < 0) {
        struct pollfd pfd = { .fd = fd, .events = POLLOUT, .revents = 0 };
        if (poll(&pfd, 1, timeoutMs) <= 0 || !(pfd.revents & POLLOUT)) {
            close(fd);
            return std::nullopt;
        }
        int err = 0;
        socklen_t errlen = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen);
        if (err != 0) {
            close(fd);
            return std::nullopt;
        }
    }

    std::string request = "GET " + path + " HTTP/1.0\r\nHost: " + host + "\r\nConnection: close\r\n\r\n";
    if (send(fd, request.data(), request.size(), 0) < 0) {
        close(fd);
        return std::nullopt;
    }

    std::string response;
    char chunk[1024];
    for (;;) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
        int pr = poll(&pfd, 1, timeoutMs);
        if (pr <= 0) {
            break;
        }
        ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            break;
        }
        response.append(chunk, static_cast<size_t>(n));
        if (response.size() > 65536) { // guard against a runaway response
            break;
        }
    }
    close(fd);

    if (response.compare(0, 5, "HTTP/") != 0) {
        return std::nullopt;
    }
    size_t space = response.find(' ');
    if (space == std::string::npos || response.compare(space + 1, 3, "200") != 0) {
        return std::nullopt;
    }
    size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return response.substr(space); // status line only, no body
    }
    return response.substr(headerEnd + 4);
}

} // namespace

namespace discovery {

std::optional<bool> fetchOccupied(const std::string &host, int port, int timeoutMs) {
    auto body = httpGet(host, port, "/status", timeoutMs);
    if (!body) {
        return std::nullopt;
    }
    // Small enough response (docs/protocol.md: {"occupied": true|false}) that
    // a substring search is simpler and more robust here than pulling in a
    // JSON parser for one boolean field.
    if (body->find("\"occupied\":true") != std::string::npos ||
        body->find("\"occupied\": true") != std::string::npos) {
        return true;
    }
    return false;
}

BeaconListener::~BeaconListener() {
    stop();
}

void BeaconListener::start(std::function<void()> onUpdateCb) {
    if (thread.joinable()) {
        return; // already running
    }
    onUpdate = std::move(onUpdateCb);
    stopFlag.store(false);
    thread = std::thread(&BeaconListener::threadMain, this);
}

void BeaconListener::stop() {
    stopFlag.store(true);
    if (thread.joinable()) {
        thread.join();
    }
}

std::vector<DiscoveredServer> BeaconListener::snapshot() {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<DiscoveredServer> out;
    out.reserve(entries.size());
    for (const auto &e : entries) {
        out.push_back(e.server);
    }
    return out;
}

void BeaconListener::threadMain() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(FINLINK_BEACON_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return;
    }

    uint8_t buf[1024];
    // Two nested time budgets: poll() itself is bounded to 200ms so
    // stopFlag is checked promptly, while onUpdate (if set) only actually
    // fires roughly once a second -- frequent enough to reflect new/pruned
    // beacons promptly without spamming brls::sync() every 200ms for
    // nothing.
    auto lastUpdateFired = std::chrono::steady_clock::now();
    while (!stopFlag.load()) {
        struct pollfd pfd = { fd, POLLIN, 0 };
        if (poll(&pfd, 1, 200) > 0 && (pfd.revents & POLLIN)) {
            struct sockaddr_in from;
            socklen_t fromLen = sizeof(from);
            ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, reinterpret_cast<struct sockaddr *>(&from), &fromLen);
            if (n > 0) {
                finlink_beacon beacon;
                if (finlink_parse_beacon(buf, static_cast<size_t>(n), &beacon)) {
                    DiscoveredServer server;
                    server.host = beacon.host;
                    server.emulatorIdentifier = beacon.emulator_identifier;
                    server.gameTitle = beacon.game_title;
                    server.streamType = beacon.stream_type;
                    server.protocolVersion = beacon.protocol_version;
                    server.handshakePort = beacon.handshake_port;
                    server.compatible = (beacon.protocol_version == FINLINK_PROTOCOL_VERSION);

                    std::lock_guard<std::mutex> lock(mutex);
                    auto now = std::chrono::steady_clock::now();
                    bool updated = false;
                    for (auto &e : entries) {
                        if (e.server.host == server.host) {
                            e.server = server;
                            e.lastSeen = now;
                            updated = true;
                            break;
                        }
                    }
                    if (!updated) {
                        entries.push_back(Entry { server, now });
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            auto now = std::chrono::steady_clock::now();
            entries.erase(std::remove_if(entries.begin(), entries.end(),
                                          [now](const Entry &e) {
                                              return std::chrono::duration_cast<std::chrono::milliseconds>(
                                                         now - e.lastSeen)
                                                         .count() > FINLINK_BEACON_STALE_MS;
                                          }),
                          entries.end());
        }

        auto now = std::chrono::steady_clock::now();
        if (onUpdate &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdateFired).count() >= 1000) {
            lastUpdateFired = now;
            onUpdate();
        }
    }

    close(fd);
}

} // namespace discovery
