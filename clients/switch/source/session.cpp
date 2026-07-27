#include "session.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

// randomGet(), not arc4random_buf(): devkitA64's newlib routes
// arc4random_buf() through getentropy(), which has no libnx syscall
// backend and fails to link. randomGet() is libnx's own CSPRNG-backed RNG.
#include <switch.h>

extern "C" {
#include "finlink/endian.h"
#include "finlink/handshake.h"
#include "finlink/inflate.h"
#include "finlink/protocol.h"
#include "finlink/websocket.h"
}

namespace {

// Same growth/consume strategy as jni_bridge.c's byte_buf, minus the
// malloc/realloc bookkeeping (std::vector does that for us); still uses
// manual front-consume via erase() rather than a deque so
// finlink_ws_parse_frame() can view the whole pending buffer as one
// contiguous pointer.
struct RecvBuffer {
    std::vector<uint8_t> data;

    void append(const uint8_t *src, size_t n) {
        data.insert(data.end(), src, src + n);
    }

    void consume(size_t n) {
        data.erase(data.begin(), data.begin() + static_cast<long>(n));
    }
};

bool send_all(int fd, const uint8_t *data, size_t size, std::atomic<bool> *stop_flag) {
    size_t sent = 0;
    while (sent < size) {
        if (stop_flag->load()) {
            return false;
        }
        ssize_t n = send(fd, data + sent, size - sent, 0);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            struct pollfd pfd = { .fd = fd, .events = POLLOUT, .revents = 0 };
            poll(&pfd, 1, 100);
            continue;
        }
        return false;
    }
    return true;
}

// Raw TCP connect + RFC6455 WS upgrade only -- no app-level handshake (see
// performAppHandshake below for that), so this can be called again against
// a redirect target without disturbing anything until it's known to have
// succeeded. On success, `leftover` holds any bytes received past the
// handshake response header -- already WebSocket frame data (the server
// sends `hello` immediately after upgrading, so this is often non-empty)
// and must feed straight into the caller's receive buffer, not be
// discarded.
bool connect_and_ws_upgrade(const std::string &host, int port, int *out_fd, std::atomic<bool> *stop_flag,
                             RecvBuffer *leftover) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo *result = nullptr;
    if (getaddrinfo(host.c_str(), port_str, &hints, &result) != 0 || !result) {
        return false;
    }

    int fd = -1;
    for (struct addrinfo *rp = result; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (::connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(result);
    if (fd < 0) {
        return false;
    }
    *out_fd = fd;

    uint8_t random_bytes[16];
    randomGet(random_bytes, sizeof(random_bytes));
    char key[FINLINK_WS_KEY_BUF_LEN];
    finlink_ws_generate_key(random_bytes, key);

    char host_header[160];
    snprintf(host_header, sizeof(host_header), "%s:%d", host.c_str(), port);

    char request[512];
    size_t request_len = finlink_ws_build_handshake_request(host_header, "/", key, request, sizeof(request));
    if (request_len == 0 || !send_all(fd, reinterpret_cast<const uint8_t *>(request), request_len, stop_flag)) {
        return false;
    }

    RecvBuffer recv_buf;
    uint8_t chunk[1024];
    finlink_ws_handshake_status status = FINLINK_WS_HANDSHAKE_INCOMPLETE;
    size_t header_len = 0;

    while (status == FINLINK_WS_HANDSHAKE_INCOMPLETE) {
        if (stop_flag->load()) {
            return false;
        }
        ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            return false;
        }
        recv_buf.append(chunk, static_cast<size_t>(n));
        if (recv_buf.data.size() > 16384) { // guard against a runaway/malformed response
            return false;
        }
        status = finlink_ws_parse_handshake_response(recv_buf.data.data(), recv_buf.data.size(), key, &header_len);
        if (status == FINLINK_WS_HANDSHAKE_ERR) {
            return false;
        }
    }

    leftover->append(recv_buf.data.data() + header_len, recv_buf.data.size() - header_len);
    return true;
}

// GC_GBA_LINK player ports are always this + the GC device number
// (docs/protocol.md; matches kPlayerBasePort in menu_activity.cpp and
// GBA_STREAM_PLAYER_BASE_PORT in the dolphin-gba-stream fork). Not shared
// with menu_activity.cpp's own constant of the same value -- this file has
// no dependency on it otherwise, and duplicating one int is simpler than
// introducing one.
constexpr int kPlayerBasePort = 6801;

// Blocks (bounded by timeoutMs, checking stop_flag throughout) until `buf`
// holds one full WebSocket frame, receiving more off `fd` as needed. Frame
// data already sitting in `buf` (e.g. left over from the WS upgrade, or a
// previous call) is tried first before any recv().
//
// Deliberately does NOT consume the frame's bytes from `buf` on success --
// out_frame->payload points *into* buf.data, and RecvBuffer::consume()
// shifts buf.data's contents down in place (erase() from the front), which
// would invalidate that pointer out from under the caller before it ever
// reads it. The caller must finish reading out_frame->payload first, then
// call buf->consume(out_frame->frame_size) itself once it's safe to
// discard -- same order threadMain's own frame handling below already uses
// for Video/Audio/Input frames (and the same bug this fixed in
// clients/android/.../jni_bridge.c's equivalent helper, found from a live
// report -- see that file's history for the postmortem).
bool receive_one_ws_frame(int fd, RecvBuffer *buf, std::atomic<bool> *stop_flag, int timeoutMs,
                           finlink_ws_frame *out_frame) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        finlink_ws_frame_status fs = finlink_ws_parse_frame(buf->data.data(), buf->data.size(), out_frame);
        if (fs == FINLINK_WS_FRAME_OK) {
            return true;
        }
        if (fs == FINLINK_WS_FRAME_ERR) {
            return false;
        }

        if (stop_flag->load()) {
            return false;
        }
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            return false;
        }
        struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
        int pr = poll(&pfd, 1, static_cast<int>(remaining.count() > 200 ? 200 : remaining.count()));
        if (pr < 0 && errno != EINTR) {
            return false;
        }
        if (pr > 0 && (pfd.revents & POLLIN)) {
            uint8_t chunk[4096];
            ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
            if (n <= 0) {
                return false;
            }
            buf->append(chunk, static_cast<size_t>(n));
        }
    }
}

constexpr int kAppHandshakeTimeoutMs = 3000;

struct AppHandshakeResult {
    bool ok = false;
    std::string reason;
};

// App-level handshake (finlink/handshake.h, docs/protocol.md
// "Verbindungsaufbau: Handshake"), run once `*fd`/`*buf` are already
// WebSocket-upgraded (`buf` may already hold the server's first message).
// On a `session_ready` with `redirect` (only possible for multi-slot
// stream types via the lobby port -- not reachable through this app's own
// "connect straight to a player port" UI, but handled anyway per
// docs/protocol.md), closes `*fd` and reconnects to the redirect target,
// repeating -- bounded to one hop, matching the protocol's own design.
AppHandshakeResult performAppHandshake(int *fd, RecvBuffer *buf, std::string *host, int *port,
                                        std::atomic<bool> *stop_flag) {
    for (int hop = 0; hop < 2; hop++) {
        finlink_ws_frame frame;
        if (!receive_one_ws_frame(*fd, buf, stop_flag, kAppHandshakeTimeoutMs, &frame)) {
            return { false, "Server hat keinen Handshake gestartet (evtl. veraltete Protokollversion)" };
        }
        if (frame.opcode != FINLINK_WS_OPCODE_TEXT ||
            finlink_peek_handshake_message(frame.payload, frame.payload_size) != FINLINK_HS_MSG_HELLO) {
            return { false, "Unerwartete erste Nachricht vom Server" };
        }

        finlink_hello hello;
        const finlink_handshake_result helloParsed = finlink_parse_hello(frame.payload, frame.payload_size, &hello);
        buf->consume(frame.frame_size); // done reading frame.payload either way
        if (helloParsed != FINLINK_HANDSHAKE_OK) {
            return { false, "hello konnte nicht gelesen werden" };
        }
        if (hello.protocol_version != FINLINK_PROTOCOL_VERSION) {
            return { false, "Server spricht Protokollversion " + std::to_string(hello.protocol_version) +
                                ", dieser Client unterstuetzt nur Version " +
                                std::to_string(FINLINK_PROTOCOL_VERSION) +
                                " -- bitte Client oder Server aktualisieren" };
        }

        // This app always dials a specific already-chosen player port (see
        // menu_activity.cpp's P1-P4 picker), never the lobby port -- so the
        // slot being asked about is simply "the one this connection is on".
        finlink_hello_ack_request ackReq {};
        ackReq.requested_slot = *port - kPlayerBasePort;
        ackReq.max_width = hello.video.width > 0 ? hello.video.width : 240;
        ackReq.max_height = hello.video.height > 0 ? hello.video.height : 160;
        ackReq.max_fps = hello.video.fps > 0 ? hello.video.fps : 60.0;
        ackReq.wants_audio = hello.has_audio;
        ackReq.max_sample_rate = hello.has_audio && hello.audio.sample_rate > 0 ? hello.audio.sample_rate : 48000;
        ackReq.max_channels = hello.has_audio && hello.audio.channels > 0 ? hello.audio.channels : 2;

        char ackJson[512];
        size_t ackLen = finlink_build_hello_ack(&ackReq, ackJson, sizeof(ackJson));
        if (ackLen == 0) {
            return { false, "hello_ack zu gross fuer den Puffer" };
        }

        uint8_t maskKey[4];
        randomGet(maskKey, sizeof(maskKey));
        uint8_t frameBuf[512 + 14];
        size_t frameLen = finlink_ws_build_frame(FINLINK_WS_OPCODE_TEXT, reinterpret_cast<const uint8_t *>(ackJson),
                                                  ackLen, maskKey, frameBuf, sizeof(frameBuf));
        if (frameLen == 0 || !send_all(*fd, frameBuf, frameLen, stop_flag)) {
            return { false, "hello_ack konnte nicht gesendet werden" };
        }

        finlink_ws_frame reply;
        if (!receive_one_ws_frame(*fd, buf, stop_flag, kAppHandshakeTimeoutMs, &reply)) {
            return { false, "keine Antwort auf hello_ack" };
        }
        if (reply.opcode != FINLINK_WS_OPCODE_TEXT) {
            return { false, "unerwartete Antwort auf hello_ack" };
        }

        const finlink_handshake_message_type replyType = finlink_peek_handshake_message(reply.payload, reply.payload_size);
        if (replyType == FINLINK_HS_MSG_HANDSHAKE_ERROR) {
            finlink_handshake_error err;
            std::string reason = finlink_parse_handshake_error(reply.payload, reply.payload_size, &err) == FINLINK_HANDSHAKE_OK
                                      ? std::string(err.detail)
                                      : std::string("Handshake vom Server abgelehnt");
            buf->consume(reply.frame_size);
            return { false, reason };
        }
        if (replyType != FINLINK_HS_MSG_SESSION_READY) {
            buf->consume(reply.frame_size);
            return { false, "unerwartete Antwort auf hello_ack" };
        }

        finlink_session_ready ready;
        const finlink_handshake_result readyParsed = finlink_parse_session_ready(reply.payload, reply.payload_size, &ready);
        buf->consume(reply.frame_size); // done reading reply.payload either way
        if (readyParsed != FINLINK_HANDSHAKE_OK) {
            return { false, "session_ready konnte nicht gelesen werden" };
        }

        if (!ready.has_redirect) {
            return { true, "" };
        }

        // Redirect: this connection carries no stream data, ever -- close
        // it, reconnect to the target, and let the loop try the same
        // exchange again there (hop 1, the only one this loop allows).
        close(*fd);
        *fd = -1;
        buf->data.clear();

        *host = ready.redirect_host;
        *port = ready.redirect_port;

        if (!connect_and_ws_upgrade(*host, *port, fd, stop_flag, buf)) {
            return { false, "Verbindung zum weitergeleiteten Port fehlgeschlagen" };
        }
        // loop: expect a fresh `hello` on the new connection
    }

    return { false, "zu viele Weiterleitungen" };
}

} // namespace

GbaSession::~GbaSession() {
    disconnect();
}

void GbaSession::connect(std::string host, int port, Listener l) {
    listener = std::move(l);
    stop.store(false);
    suppressDisconnectedCallback.store(false);
    thread = std::thread(&GbaSession::threadMain, this, std::move(host), port);
}

void GbaSession::sendInput(uint16_t keyMask) {
    pendingKeymask.store(keyMask);
    inputDirty.store(true);
}

void GbaSession::disconnect() {
    suppressDisconnectedCallback.store(true);
    stop.store(true);
    if (thread.joinable()) {
        thread.join();
    }
}

void GbaSession::threadMain(std::string host, int port) {
    RecvBuffer buf;
    if (!connect_and_ws_upgrade(host, port, &sockfd, &stop, &buf)) {
        if (sockfd >= 0) {
            close(sockfd);
            sockfd = -1;
        }
        if (listener.onDisconnected && !suppressDisconnectedCallback.load()) {
            listener.onDisconnected("Verbindung fehlgeschlagen");
        }
        return;
    }

    // App-level handshake (finlink/handshake.h): must succeed -- version
    // match, this slot requested and free -- before any Video/Audio/Input
    // binary frame is allowed on this connection. May reconnect `sockfd`/
    // `host`/`port` once, on a redirect (see performAppHandshake).
    AppHandshakeResult hs = performAppHandshake(&sockfd, &buf, &host, &port, &stop);
    if (!hs.ok) {
        if (sockfd >= 0) {
            close(sockfd);
            sockfd = -1;
        }
        if (listener.onDisconnected && !suppressDisconnectedCallback.load()) {
            listener.onDisconnected(hs.reason);
        }
        return;
    }

    if (listener.onConnected) {
        listener.onConnected();
    }

    // inflate_buf is scratch space for finlink_inflate_raw()'s output,
    // whose content depends on hdr.format (raw RGB565, or a palette +
    // per-pixel indices, see finlink/protocol.h) -- rgb565_out is always
    // final width*height RGB565 pixels, decoded from that via
    // finlink_decode_video_frame().
    std::vector<uint8_t> inflate_buf;
    std::vector<uint8_t> rgb565_out;
    uint8_t chunk[4096];

    while (!stop.load()) {
        struct pollfd pfd = { .fd = sockfd, .events = POLLIN, .revents = 0 };
        int pr = poll(&pfd, 1, 4); // short timeout: also need to notice pending input to send
        if (pr > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = recv(sockfd, chunk, sizeof(chunk), 0);
            if (n <= 0) {
                break; // peer closed or socket error
            }
            buf.append(chunk, static_cast<size_t>(n));
        } else if (pr < 0 && errno != EINTR) {
            break;
        }

        bool should_stop = false;
        for (;;) {
            finlink_ws_frame frame;
            finlink_ws_frame_status fs = finlink_ws_parse_frame(buf.data.data(), buf.data.size(), &frame);
            if (fs == FINLINK_WS_FRAME_INCOMPLETE) {
                break;
            }
            if (fs == FINLINK_WS_FRAME_ERR) {
                should_stop = true;
                break;
            }
            if (frame.opcode == FINLINK_WS_OPCODE_CLOSE) {
                buf.consume(frame.frame_size);
                should_stop = true;
                break;
            }

            finlink_msg_type type;
            if (finlink_peek_type(frame.payload, frame.payload_size, &type) == FINLINK_OK) {
                if (type == FINLINK_MSG_VIDEO && listener.onVideoFrame) {
                    finlink_video_header hdr;
                    if (finlink_parse_video_header(frame.payload, frame.payload_size, &hdr) == FINLINK_OK) {
                        // rgb565_out is the PERSISTENT framebuffer: resizing
                        // it to the same size every frame (width/height
                        // don't change mid-stream) is a no-op that leaves
                        // its content alone, which is exactly what a
                        // FINLINK_VIDEO_FORMAT_TILES frame needs -- it only
                        // patches the tiles it lists, every other pixel must
                        // keep whatever the previous frame decoded there.
                        size_t framebuffer_size = static_cast<size_t>(hdr.width) * hdr.height * 2;
                        inflate_buf.resize(finlink_video_max_inflated_size(hdr.width, hdr.height));
                        rgb565_out.resize(framebuffer_size);
                        size_t inflated_size = 0;
                        if (finlink_inflate_raw(hdr.compressed_data, hdr.compressed_size, inflate_buf.data(),
                                                 inflate_buf.size(), &inflated_size) == FINLINK_INFLATE_OK &&
                            finlink_decode_video_frame(hdr.format, inflate_buf.data(), inflated_size, hdr.width,
                                                        hdr.height, rgb565_out.data(),
                                                        rgb565_out.size()) == FINLINK_OK) {
                            listener.onVideoFrame(hdr.width, hdr.height, rgb565_out);
                        }
                    }
                } else if (type == FINLINK_MSG_AUDIO && listener.onAudioFrame) {
                    finlink_audio_frame audio;
                    if (finlink_parse_audio_frame(frame.payload, frame.payload_size, &audio) == FINLINK_OK &&
                        audio.sample_count > 0) {
                        std::vector<int16_t> pcm(audio.sample_count);
                        for (size_t i = 0; i < audio.sample_count; i++) {
                            pcm[i] = finlink_read_s16le(audio.samples + i * 2);
                        }
                        listener.onAudioFrame(audio.sample_rate, audio.channels, std::move(pcm));
                    }
                }
            }

            buf.consume(frame.frame_size);
        }
        if (should_stop) {
            break;
        }

        if (inputDirty.exchange(false)) {
            uint16_t mask = pendingKeymask.load();
            uint8_t payload[FINLINK_INPUT_FRAME_SIZE];
            finlink_build_input_frame(mask, payload);

            uint8_t mask_key[4];
            randomGet(mask_key, sizeof(mask_key));

            uint8_t frame_buf[FINLINK_INPUT_FRAME_SIZE + 10];
            size_t frame_len = finlink_ws_build_frame(FINLINK_WS_OPCODE_BINARY, payload, sizeof(payload), mask_key,
                                                        frame_buf, sizeof(frame_buf));
            if (frame_len > 0) {
                send_all(sockfd, frame_buf, frame_len, &stop);
            }
        }
    }

    close(sockfd);
    sockfd = -1;
    if (listener.onDisconnected && !suppressDisconnectedCallback.load()) {
        listener.onDisconnected("Verbindung getrennt");
    }
}
