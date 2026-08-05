#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// LAN discovery + lobby status polling, mirroring
// clients/switch/source/discovery.hpp.
namespace discovery {

// This console's own IPv4 address as a dotted-quad string, or empty if
// gethostid() couldn't report one (no Wi-Fi connection, soc not
// initialized, ...). Meant as an on-screen diagnostic: if this comes back
// empty, 0.0.0.0 or a 127.x loopback address, no connect attempt to
// anything is going to work, which looks identical from the UI's side to
// "the streaming host has no free slot" -- this makes the difference
// visible.
std::string localIpString();

// GET /status on host:port (one of the four player ports, 6801-6804).
// nullopt = unreachable; otherwise the "occupied" field from the JSON body.
std::optional<bool> fetchOccupied(const std::string &host, int port, int timeoutMs = 1500);

// One server currently announcing itself via UDP beacon (unison/discovery.h,
// docs/protocol.md "Discovery-Beacon (UDP)").
struct DiscoveredServer {
    std::string host;
    std::string emulatorIdentifier;
    std::string gameTitle;
    std::string streamType;
    int protocolVersion = 0;
    int handshakePort = 0;
    // protocolVersion == UNISON_PROTOCOL_VERSION (exact match, per
    // docs/protocol.md) -- callers should grey out/refuse to connect to an
    // incompatible entry rather than hide it, so an old/new server is at
    // least visible instead of silently absent.
    bool compatible = false;
};

// Listens for Unison UDP discovery beacons on a background thread for as
// long as start()/stop() bracket it, mirroring GbaSession's thread-owning
// style -- start()/stop() are cheap enough to call every time the menu
// screen becomes visible/hidden rather than needing a manual "search"
// button the way the old subnet-sweep discovery did (a server announces
// itself every ~2s on its own, see docs/protocol.md, so there's nothing
// left to actively trigger). snapshot() is safe to call from the main/
// render thread at any time, including while stopped (returns whatever
// was last seen before stop() -- a stale list, not necessarily empty).
class BeaconListener {
  public:
    ~BeaconListener();

    void start();
    void stop();
    std::vector<DiscoveredServer> snapshot();

  private:
    struct Entry {
        DiscoveredServer server;
        std::chrono::steady_clock::time_point lastSeen;
    };

    std::thread thread;
    std::atomic<bool> stopFlag { false };
    std::mutex mutex;
    std::vector<Entry> entries;

    void threadMain();
};

} // namespace discovery
