#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// LAN discovery + lobby status polling, mirroring
// clients/3ds/source/discovery.hpp.
namespace discovery {

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
// style. onUpdate (optional) is invoked from that background thread on a
// ~1s heartbeat -- borealis's own UI mutation model needs every update
// pushed via brls::sync() onto pre-existing views rather than polled once
// per frame the way clients/3ds does it (see menu_activity.hpp's own
// comment on why views are never constructed at runtime here), so the
// caller is expected to brls::sync([this]{ ...call snapshot()... }) inside
// this callback rather than polling snapshot() from a render loop.
class BeaconListener {
  public:
    ~BeaconListener();

    void start(std::function<void()> onUpdate = nullptr);
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
    std::function<void()> onUpdate;

    void threadMain();
};

} // namespace discovery
