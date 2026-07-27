#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

// Owns the raw BSD socket, runs the connect/handshake/receive loop on a
// background thread, and invokes callbacks for video/audio/connection
// events. All protocol/codec logic (WS handshake+framing, message parsing,
// deflate) lives in core/ -- this is deliberately "dumb" I/O plumbing.
// Portable, unchanged in spirit from clients/switch/source/session.hpp.
//
// Every callback runs on this background thread, not the main/render
// thread -- callers must only touch mutex-protected shared state from
// them (see video_tex.hpp's pattern), never citro2d/GPU calls directly.
class GbaSession {
  public:
    struct Listener {
        // streamType is the server's hello.stream_type (finlink/handshake.h,
        // docs/protocol.md "Stream-Typen") -- e.g. "GC_GBA_LINK" -- so the
        // caller can decide which physical screen to show video on (see
        // finlink_stream_type_prefers_secondary_screen() and main.cpp).
        std::function<void(std::string streamType)> onConnected;
        std::function<void(uint32_t width, uint32_t height, std::vector<uint8_t> rgb565)> onVideoFrame;
        std::function<void(uint32_t sampleRate, uint8_t channels, std::vector<int16_t> pcm)> onAudioFrame;
        std::function<void(std::string reason)> onDisconnected;
    };

    ~GbaSession();

    // Starts the background thread. Only one connection at a time; call
    // disconnect() before reusing this object.
    void connect(std::string host, int port, Listener listener);

    // Merges into whatever mask is already pending and marks it dirty;
    // sent from the session thread's own loop, not from here, so this
    // never touches the socket directly.
    void sendInput(uint16_t keyMask);

    void disconnect();

  private:
    std::thread thread;
    std::atomic<bool> stop { false };
    std::atomic<uint16_t> pendingKeymask { 0 };
    std::atomic<bool> inputDirty { false };
    int sockfd = -1;
    Listener listener;

    void threadMain(std::string host, int port);
};
