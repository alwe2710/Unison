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
// deflate) lives in core/ -- this is deliberately "dumb" I/O plumbing,
// the C++ equivalent of clients/android/.../jni_bridge.c.
//
// onVideoFrame/onConnected/onDisconnected run on the session's own
// background thread, not the render thread -- callers touching borealis
// UI state from them must hop through brls::sync(), same reasoning as the
// Android client's runOnUiThread(). onAudioFrame is the one exception
// (writing to audout from a background thread is exactly what it's for).
class GbaSession {
  public:
    struct Listener {
        // grantedVideoMode is session_ready.video_mode verbatim -- empty if
        // the server predates that field entirely (see docs/protocol.md
        // "Video-mode fallback"). Compare against whatever videoMode was
        // passed to connect() below to decide whether to prompt: skip the
        // comparison entirely if this is empty, don't treat empty as
        // "tiles was granted".
        std::function<void(std::string grantedVideoMode)> onConnected;
        std::function<void(uint32_t width, uint32_t height, std::vector<uint8_t> rgb565)> onVideoFrame;
        std::function<void(uint32_t sampleRate, uint8_t channels, std::vector<int16_t> pcm)> onAudioFrame;
        std::function<void(std::string reason)> onDisconnected;
    };

    ~GbaSession();

    // Starts the background thread. Only one connection at a time; call
    // disconnect() before reusing this object. videoMode is sent verbatim
    // as hello_ack.video_mode (finlink/docs/protocol.md).
    void connect(std::string host, int port, std::string videoMode, Listener listener);

    // Merges into whatever mask is already pending and marks it dirty;
    // sent from the session thread's own loop, not from here, so this
    // never touches the socket directly (same single-writer rationale as
    // jni_bridge.c's maybe_send_input()).
    void sendInput(uint16_t keyMask);

    // Callers must not touch anything the Listener's callbacks captured
    // (e.g. `this` of the object that called connect()) once this returns
    // -- it joins the thread, but a queued brls::sync() from
    // onDisconnected could otherwise still run later against a
    // now-dangling capture. To prevent that, disconnect() suppresses the
    // final onDisconnected call entirely: it only ever fires for a drop
    // this session didn't ask for, matching
    // clients/android/.../jni_bridge.c's comment on the same tradeoff.
    void disconnect();

  private:
    std::thread thread;
    std::atomic<bool> stop { false };
    std::atomic<bool> suppressDisconnectedCallback { false };
    std::atomic<uint16_t> pendingKeymask { 0 };
    std::atomic<bool> inputDirty { false };
    int sockfd = -1;
    Listener listener;

    void threadMain(std::string host, int port, std::string videoMode);
};
