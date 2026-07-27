#include "discovery.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <3ds.h>

namespace {

// Minimal blocking-with-timeout HTTP/1.0 GET, identical to
// clients/switch/source/discovery.cpp's -- not a general-purpose HTTP
// client, just enough for this protocol's two tiny lobby endpoints.
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
        // Only bail out on an actual timeout (poll() returning 0). Once it
        // returns at all, SO_ERROR is the authoritative way to tell success
        // from failure -- requiring POLLOUT specifically in revents is
        // pickier than it needs to be, and 3DS's soc:u connect-completion
        // notification isn't guaranteed to look exactly like glibc's.
        if (poll(&pfd, 1, timeoutMs) <= 0) {
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
        return response.substr(space); // status line only, no body -- fine for probeLobby
    }
    return response.substr(headerEnd + 4);
}

// gethostid() (libctru's soc.h) returns this console's own IPv4 address in
// network byte order, same as struct in_addr.s_addr. 0 and -1 both mean
// "no address to report" (no Wi-Fi connection, soc not initialized, ...).
uint32_t getLocalIp() {
    long hostidRaw = gethostid();
    if (hostidRaw == 0 || hostidRaw == -1) {
        return 0;
    }
    return static_cast<uint32_t>(hostidRaw);
}

} // namespace

namespace discovery {

std::vector<std::string> localSubnetHosts() {
    std::vector<std::string> hosts;

    uint32_t raw = getLocalIp();
    if (raw == 0) {
        return hosts;
    }

    // There's no equivalent of Switch's nifmGetCurrentIpConfigInfo() to
    // query the real subnet mask here, so this assumes the near-universal
    // home-network /24 rather than not offering discovery at all.
    uint32_t ip = ntohl(raw);
    uint32_t network = ip & 0xFFFFFF00u; // /24

    hosts.reserve(254);
    for (uint32_t offset = 1; offset <= 254; offset++) {
        uint32_t hostIp = htonl(network + offset);
        char buf[INET_ADDRSTRLEN];
        struct in_addr in;
        in.s_addr = hostIp;
        inet_ntop(AF_INET, &in, buf, sizeof(buf));
        hosts.emplace_back(buf);
    }
    return hosts;
}

std::string localIpString() {
    uint32_t raw = getLocalIp();
    if (raw == 0) {
        return "";
    }
    char buf[INET_ADDRSTRLEN];
    struct in_addr in;
    in.s_addr = raw;
    inet_ntop(AF_INET, &in, buf, sizeof(buf));
    return std::string(buf);
}

bool probeLobby(const std::string &ip, int timeoutMs) {
    return httpGet(ip, 6800, "/", timeoutMs).has_value();
}

std::optional<bool> fetchOccupied(const std::string &host, int port, int timeoutMs) {
    auto body = httpGet(host, port, "/status", timeoutMs);
    if (!body) {
        return std::nullopt;
    }
    if (body->find("\"occupied\":true") != std::string::npos ||
        body->find("\"occupied\": true") != std::string::npos) {
        return true;
    }
    return false;
}

} // namespace discovery
