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
        // streamType is the server's hello.stream_type (unison/handshake.h,
        // docs/protocol.md "Stream-Typen") -- e.g. "GC_GBA_LINK" -- so the
        // caller can decide which physical screen to show video on (see
        // unison_stream_type_prefers_secondary_screen() and main.cpp).
        // grantedVideoMode is session_ready.video_mode verbatim -- empty if
        // the server predates that field entirely (see docs/protocol.md
        // "Video-mode fallback"). Compare against whatever videoMode was
        // passed to connect() below to decide whether to prompt: skip the
        // comparison entirely if this is empty, don't treat empty as
        // "tiles was granted".
        std::function<void(std::string streamType, std::string grantedVideoMode)> onConnected;
        std::function<void(uint32_t width, uint32_t height, std::vector<uint8_t> rgb565)> onVideoFrame;
        // UNISON_VIDEO_FORMAT_H264 only (mutually exclusive with
        // onVideoFrame above) -- there's no UNISON_VIDEO_FORMAT_H265
        // counterpart on this client: the 3DS's MVD hardware decoder
        // (New3DS-exclusive, see h264_decoder.hpp) only ever supported
        // H.264, since the console predates HEVC entirely, and software
        // HEVC decode on the 3DS's ARM11 CPU isn't remotely practical.
        // data is a raw Annex-B NAL stream straight from the server's
        // encoder, not raw-deflate, copied into this vector the same way
        // onVideoFrame's rgb565/onAudioFrame's pcm already are. width/height
        // are the encoder's *coded* dimensions -- see
        // SoftwareVideoEncoder::CodedWidth()'s own comment on the host
        // repos.
        std::function<void(uint32_t width, uint32_t height, std::vector<uint8_t> data)> onCompressedVideoFrame;
        std::function<void(uint32_t sampleRate, uint8_t channels, std::vector<int16_t> pcm)> onAudioFrame;
        std::function<void(std::string reason)> onDisconnected;
    };

    ~GbaSession();

    // Starts the background thread. Only one connection at a time; call
    // disconnect() before reusing this object. videoMode is sent verbatim
    // as hello_ack.video_mode (unison/docs/protocol.md).
    void connect(std::string host, int port, std::string videoMode, Listener listener);

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

    void threadMain(std::string host, int port, std::string videoMode);
};
