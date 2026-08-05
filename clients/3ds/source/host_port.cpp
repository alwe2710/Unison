#include "host_port.hpp"

#include <cstdlib>

std::optional<HostPort> splitHostPort(const std::string &raw) {
    size_t colon = raw.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon == raw.size() - 1) {
        return std::nullopt;
    }
    char *end = nullptr;
    long port = strtol(raw.c_str() + colon + 1, &end, 10);
    if (*end != '\0' || port <= 0 || port > 65535) {
        return std::nullopt;
    }
    return HostPort { raw.substr(0, colon), static_cast<int>(port) };
}
