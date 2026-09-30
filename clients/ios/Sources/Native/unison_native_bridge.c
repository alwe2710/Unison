// MVP subset of clients/android/.../jni_bridge.c, stripped to what this
// client's first vertical slice actually needs (see clients/ios/README.md's
// "Phasing" section): touch/extended-input is implemented (unlike the
// original gba_buttons-only MVP), h264/h265 is now handed off raw to
// Swift's own VideoToolbox decode via on_compressed_video_frame() below
// (see that callback's own comment), and the server's on-screen-keyboard
// request/response round-trip is implemented via on_text_input_request()/
// unison_native_send_text_input_response() -- still no mic input. No
// video-window/Surface concept at all (Swift owns rendering, driven by
// the on_video_frame/on_compressed_video_frame callbacks -- there's no
// MediaCodec-equivalent "decode straight into a window" fast path to
// bypass here, AVSampleBufferDisplayLayer plays the same role from the
// Swift side instead). What's still deferred isn't forgotten -- ported
// back in once each proves useful (see UnisonNativeBridgeTests.swift for
// the connect/handshake/decode/input loop's own proof).
//
// Same core split as jni_bridge.c: this file is I/O and callback plumbing
// only, all protocol/codec logic lives in unison_core (core/src/*.c).
#include "unison_native_bridge.h"

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "unison/endian.h"
#include "unison/handshake.h"
#include "unison/inflate.h"
#include "unison/protocol.h"
#include "unison/websocket.h"

typedef struct {
    uint8_t *data;
    size_t len;
    size_t capacity;
} byte_buf;

static void byte_buf_reserve(byte_buf *b, size_t extra) {
    if (b->len + extra <= b->capacity) {
        return;
    }
    size_t new_cap = b->capacity == 0 ? 4096 : b->capacity * 2;
    while (new_cap < b->len + extra) {
        new_cap *= 2;
    }
    b->data = realloc(b->data, new_cap);
    b->capacity = new_cap;
}

static void byte_buf_append(byte_buf *b, const uint8_t *src, size_t n) {
    byte_buf_reserve(b, n);
    memcpy(b->data + b->len, src, n);
    b->len += n;
}

static void byte_buf_consume(byte_buf *b, size_t n) {
    memmove(b->data, b->data + n, b->len - n);
    b->len -= n;
}

static void byte_buf_free(byte_buf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->capacity = 0;
}

struct unison_native_client {
    char host[128];
    int port;
    char video_mode[UNISON_VIDEO_MODE_LEN];
    unison_native_callbacks callbacks;
    int sockfd;
    // Dedicated video/audio channel (docs/protocol.md, "Dedicated
    // video/audio channel (UDP)", protocol_version 4) -- a connected
    // SOCK_DGRAM socket, -1 whenever the server didn't offer
    // session_ready.video_port, in which case Video/Audio keep arriving on
    // sockfd above exactly as in protocol_version <=2. Only ever touched by
    // client_thread_main()/run_session_loop() and the functions they call,
    // same "one owner" reasoning as sockfd itself.
    int video_sockfd;
    // The rendezvous handshake (udp_rendezvous(), client_thread_main) had
    // to read *some* datagram off the socket to know it succeeded -- that
    // datagram is real Video/Audio data, stashed here so run_session_loop()
    // can process it instead of it being silently dropped.
    uint8_t pending_first_video_datagram[UNISON_UDP_MAX_DATAGRAM_SIZE];
    ssize_t pending_first_video_datagram_len;
    pthread_t thread;
    atomic_bool stop;
    atomic_int pending_keymask;
    atomic_bool input_dirty;
    // Set once from app_handshake_result right after a successful
    // handshake, read by maybe_send_touch() to pick which of three touch
    // frame shapes to build -- same three-way selection, and the same
    // "extended_input implies has_buttons" relationship, as jni_bridge.c's
    // own maybe_send_touch().
    atomic_bool touch_input;
    atomic_bool has_buttons;
    atomic_bool extended_input;
    atomic_int pending_touch_x;
    atomic_int pending_touch_y;
    atomic_bool pending_touch_pressed;
    atomic_bool touch_dirty;
    // Buttons/sticks are NOT gated on touch's own pressed state (e.g.
    // holding a button with no finger on the touch area at all is a real,
    // valid, independent input) -- same as jni_bridge.c's own
    // nativeSendExtendedInput/maybe_send_touch split. Meaningless (left
    // at zero) whenever has_buttons is false.
    atomic_int pending_buttons;
    atomic_int pending_left_x;
    atomic_int pending_left_y;
    atomic_int pending_right_x;
    atomic_int pending_right_y;
    // Text-input response: a one-shot dynamically-sized value (unlike the
    // fixed-size atomics above), so it needs a mutex rather than plain
    // atomics -- same shape as jni_bridge.c's own
    // pending_text_response_text/_len/_confirmed +
    // pending_text_response_mutex.
    pthread_mutex_t pending_text_response_mutex;
    char *pending_text_response_text; // NULL/0 until a response is queued
    size_t pending_text_response_len;
    bool pending_text_response_confirmed;
    atomic_bool text_response_dirty;
};

static bool send_all(int fd, const uint8_t *data, size_t size, atomic_bool *stop_flag) {
    size_t sent = 0;
    while (sent < size) {
        if (atomic_load(stop_flag)) {
            return false;
        }
        ssize_t n = send(fd, data + sent, size - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            struct pollfd pfd = {.fd = fd, .events = POLLOUT};
            poll(&pfd, 1, 100);
            continue;
        }
        return false;
    }
    return true;
}

// Raw TCP connect + RFC6455 WS upgrade only -- no app-level handshake (see
// perform_app_handshake below). Same shape as jni_bridge.c's own
// connect_and_ws_upgrade(); see that function's comment for the leftover/
// redirect-hop rationale, ported verbatim.
static bool connect_and_ws_upgrade(const char *host, int port, atomic_bool *stop_flag, int *out_fd,
                                    byte_buf *leftover) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo *result = NULL;
    if (getaddrinfo(host, port_str, &hints, &result) != 0 || !result) {
        return false;
    }

    int fd = -1;
    for (struct addrinfo *rp = result; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(result);
    if (fd < 0) {
        return false;
    }

    uint8_t random_bytes[16];
    arc4random_buf(random_bytes, sizeof(random_bytes));
    char key[UNISON_WS_KEY_BUF_LEN];
    unison_ws_generate_key(random_bytes, key);

    char host_header[160];
    snprintf(host_header, sizeof(host_header), "%s:%d", host, port);

    char request[512];
    size_t request_len =
        unison_ws_build_handshake_request(host_header, "/", key, request, sizeof(request));
    if (request_len == 0 || !send_all(fd, (const uint8_t *)request, request_len, stop_flag)) {
        close(fd);
        return false;
    }

    byte_buf recv_buf = {0};
    uint8_t chunk[1024];
    unison_ws_handshake_status status = UNISON_WS_HANDSHAKE_INCOMPLETE;
    size_t header_len = 0;

    while (status == UNISON_WS_HANDSHAKE_INCOMPLETE) {
        if (atomic_load(stop_flag)) {
            byte_buf_free(&recv_buf);
            close(fd);
            return false;
        }
        ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            byte_buf_free(&recv_buf);
            close(fd);
            return false;
        }
        byte_buf_append(&recv_buf, chunk, (size_t)n);
        if (recv_buf.len > 16384) { // guard against a runaway/malformed response
            byte_buf_free(&recv_buf);
            close(fd);
            return false;
        }
        status = unison_ws_parse_handshake_response(recv_buf.data, recv_buf.len, key, &header_len);
        if (status == UNISON_WS_HANDSHAKE_ERR) {
            byte_buf_free(&recv_buf);
            close(fd);
            return false;
        }
    }

    byte_buf_append(leftover, recv_buf.data + header_len, recv_buf.len - header_len);
    byte_buf_free(&recv_buf);
    *out_fd = fd;
    return true;
}

// Creates a SOCK_DGRAM socket and connect()s it to host:port -- for UDP,
// connect() just fixes the peer address kernel-side so plain send()/recv()
// work without per-call addressing (docs/protocol.md, "Dedicated
// video/audio channel (UDP)"), it does not perform a handshake the way TCP
// connect() does. Same shape as jni_bridge.c's own
// create_and_connect_udp_socket().
static bool create_and_connect_udp_socket(const char *host, int port, int *out_fd) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo *result = NULL;
    if (getaddrinfo(host, port_str, &hints, &result) != 0 || !result) {
        return false;
    }

    int fd = -1;
    for (struct addrinfo *rp = result; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
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
    return true;
}

// Rendezvous handshake for the UDP video/audio channel (docs/protocol.md) --
// sends UNISON_MSG_UDP_HELLO (empty payload, fragment_index/fragment_count
// both 0) every 200ms until either the first real datagram arrives back or
// timeout_ms elapses. The first datagram received back is *real*
// Video/Audio data (the server starts streaming as soon as it's learned
// the client's address, it doesn't ack the hello specifically) -- copied
// into out_buf/*out_len rather than just used as a yes/no rendezvous
// signal, so the caller can still process it instead of silently dropping
// the first frame. Same shape as jni_bridge.c's own udp_rendezvous().
static bool udp_rendezvous(int fd, atomic_bool *stop_flag, int timeout_ms, uint8_t *out_buf,
                            size_t out_buf_cap, ssize_t *out_len) {
    const unison_udp_fragment_header hello_hdr = {
        .msg_type = UNISON_MSG_UDP_HELLO, .frame_id = 0, .fragment_index = 0, .fragment_count = 0};
    uint8_t hello_datagram[UNISON_UDP_FRAGMENT_HEADER_SIZE];
    unison_build_udp_fragment_header(&hello_hdr, hello_datagram);

    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    for (;;) {
        if (atomic_load(stop_flag)) {
            return false;
        }
        send(fd, hello_datagram, sizeof(hello_datagram), 0);

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long remaining_ms =
            (deadline.tv_sec - now.tv_sec) * 1000 + (deadline.tv_nsec - now.tv_nsec) / 1000000;
        if (remaining_ms <= 0) {
            return false;
        }

        struct pollfd pfd = {.fd = fd, .events = POLLIN};
        int wait_ms = remaining_ms < 200 ? (int)remaining_ms : 200;
        int pr = poll(&pfd, 1, wait_ms);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = recv(fd, out_buf, out_buf_cap, 0);
            if (n > 0) {
                *out_len = n;
                return true;
            }
        }
    }
}

static bool receive_one_ws_frame(unison_native_client *c, byte_buf *buf, unison_ws_frame *out_frame,
                                  int timeout_ms) {
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    for (;;) {
        unison_ws_frame_status fs = unison_ws_parse_frame(buf->data, buf->len, out_frame);
        if (fs == UNISON_WS_FRAME_OK) {
            return true;
        }
        if (fs == UNISON_WS_FRAME_ERR) {
            return false;
        }

        if (atomic_load(&c->stop)) {
            return false;
        }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long remaining_ms = (long)(deadline.tv_sec - now.tv_sec) * 1000 +
                             (deadline.tv_nsec - now.tv_nsec) / 1000000;
        if (remaining_ms <= 0) {
            return false;
        }

        struct pollfd pfd = {.fd = c->sockfd, .events = POLLIN};
        int pr = poll(&pfd, 1, remaining_ms > 200 ? 200 : (int)remaining_ms);
        if (pr < 0 && errno != EINTR) {
            return false;
        }
        if (pr > 0 && (pfd.revents & POLLIN)) {
            uint8_t chunk[4096];
            ssize_t n = recv(c->sockfd, chunk, sizeof(chunk), 0);
            if (n <= 0) {
                return false;
            }
            byte_buf_append(buf, chunk, (size_t)n);
        }
    }
}

// GC_GBA_LINK player ports are always this + the GC device number
// (docs/protocol.md; matches GbaStreamClient.PLAYER_BASE_PORT on Android
// and GBA_STREAM_PLAYER_BASE_PORT in the dolphin-gba-stream fork) -- the
// only stream type this MVP bridge connects to, same reasoning as
// jni_bridge.c's own UNISON_GBA_LINK_PLAYER_BASE_PORT.
#define UNISON_GBA_LINK_PLAYER_BASE_PORT 6801
#define APP_HANDSHAKE_TIMEOUT_MS 3000

typedef struct {
    bool ok;
    char reason[160];
    int32_t width;
    int32_t height;
    bool touch_input;
    bool extended_input;
    bool has_buttons;
    char granted_video_mode[UNISON_VIDEO_MODE_LEN];
    // hello.stream_type verbatim -- PlayerView needs this (not just the
    // touch_input/has_buttons/extended_input booleans derived from it) to
    // look up Prefs.bilinear(for:), which is keyed per stream_type string
    // (KeyBindingsView/AntialiasingView's own per-console rows). Two
    // different stream types (WIIU_GAMEPAD, N3DS_BOTTOM_SCREEN) share the
    // exact same touch_input/has_buttons/extended_input=true/true/true
    // shape, so those three booleans alone can't tell Cemu and Azahar
    // apart the way the raw string can.
    char stream_type[UNISON_STREAM_TYPE_LEN];
    // Dedicated video/audio channel (docs/protocol.md, "Dedicated
    // video/audio channel (UDP)", protocol_version 4) -- has_video_port
    // false whenever the server didn't offer session_ready.video_port, in
    // which case Video/Audio keep arriving on the control connection
    // exactly as in protocol_version <=2.
    bool has_video_port;
    int video_port;
} app_handshake_result;

// App-level handshake (unison/handshake.h, docs/protocol.md
// "Verbindungsaufbau: Handshake") -- ported from jni_bridge.c's
// perform_app_handshake(), same single-redirect-hop behavior and (kept
// verbatim, matching that file's own precedent of user-facing German
// reason strings) error text.
static app_handshake_result perform_app_handshake(unison_native_client *c, byte_buf *buf) {
    app_handshake_result result = {false, ""};

    for (int hop = 0; hop < 2; hop++) {
        unison_ws_frame frame;
        if (!receive_one_ws_frame(c, buf, &frame, APP_HANDSHAKE_TIMEOUT_MS)) {
            snprintf(result.reason, sizeof(result.reason),
                     "Server hat keinen Handshake gestartet (evtl. veraltete Protokollversion)");
            return result;
        }
        if (frame.opcode != UNISON_WS_OPCODE_TEXT ||
            unison_peek_handshake_message(frame.payload, frame.payload_size) != UNISON_HS_MSG_HELLO) {
            snprintf(result.reason, sizeof(result.reason), "Unerwartete erste Nachricht vom Server");
            return result;
        }

        unison_hello hello;
        const unison_handshake_result hello_parsed =
            unison_parse_hello(frame.payload, frame.payload_size, &hello);
        byte_buf_consume(buf, frame.frame_size);
        if (hello_parsed != UNISON_HANDSHAKE_OK) {
            snprintf(result.reason, sizeof(result.reason), "hello konnte nicht gelesen werden");
            return result;
        }
        if (hello.protocol_version != UNISON_PROTOCOL_VERSION) {
            snprintf(result.reason, sizeof(result.reason),
                     "Server spricht Protokollversion %d, dieser Client unterstuetzt nur Version %d "
                     "-- bitte Client oder Server aktualisieren",
                     hello.protocol_version, UNISON_PROTOCOL_VERSION);
            return result;
        }
        result.extended_input = strcmp(hello.input_encoding, "n3ds_touch_and_buttons") == 0;
        result.has_buttons =
            result.extended_input || strcmp(hello.input_encoding, "touch_and_buttons") == 0;
        result.touch_input =
            result.has_buttons || strcmp(hello.input_encoding, "n3ds_touch") == 0;
        strncpy(result.stream_type, hello.stream_type, sizeof(result.stream_type) - 1);
        result.stream_type[sizeof(result.stream_type) - 1] = '\0';

        unison_hello_ack_request ack_req;
        memset(&ack_req, 0, sizeof(ack_req));
        ack_req.requested_slot = strcmp(hello.stream_type, "GC_GBA_LINK") == 0
                                      ? c->port - UNISON_GBA_LINK_PLAYER_BASE_PORT
                                      : 0;
        ack_req.max_width = hello.video.width > 0 ? hello.video.width : 240;
        ack_req.max_height = hello.video.height > 0 ? hello.video.height : 160;
        ack_req.max_fps = hello.video.fps > 0 ? hello.video.fps : 60.0;
        ack_req.wants_audio = hello.has_audio;
        ack_req.max_sample_rate = hello.has_audio && hello.audio.sample_rate > 0 ? hello.audio.sample_rate : 48000;
        ack_req.max_channels = hello.has_audio && hello.audio.channels > 0 ? hello.audio.channels : 2;
        strncpy(ack_req.video_mode, c->video_mode, sizeof(ack_req.video_mode) - 1);

        char ack_json[512];
        size_t ack_len = unison_build_hello_ack(&ack_req, ack_json, sizeof(ack_json));
        if (ack_len == 0) {
            snprintf(result.reason, sizeof(result.reason), "hello_ack zu gross fuer den Puffer");
            return result;
        }

        uint8_t mask_key[4];
        arc4random_buf(mask_key, sizeof(mask_key));
        uint8_t frame_buf[512 + 14];
        size_t frame_len = unison_ws_build_frame(UNISON_WS_OPCODE_TEXT, (const uint8_t *)ack_json,
                                                   ack_len, mask_key, frame_buf, sizeof(frame_buf));
        if (frame_len == 0 || !send_all(c->sockfd, frame_buf, frame_len, &c->stop)) {
            snprintf(result.reason, sizeof(result.reason), "hello_ack konnte nicht gesendet werden");
            return result;
        }

        unison_ws_frame reply;
        if (!receive_one_ws_frame(c, buf, &reply, APP_HANDSHAKE_TIMEOUT_MS)) {
            snprintf(result.reason, sizeof(result.reason), "keine Antwort auf hello_ack");
            return result;
        }
        if (reply.opcode != UNISON_WS_OPCODE_TEXT) {
            snprintf(result.reason, sizeof(result.reason), "unerwartete Antwort auf hello_ack");
            return result;
        }

        const unison_handshake_message_type reply_type =
            unison_peek_handshake_message(reply.payload, reply.payload_size);
        if (reply_type == UNISON_HS_MSG_HANDSHAKE_ERROR) {
            unison_handshake_error err;
            const unison_handshake_result err_parsed =
                unison_parse_handshake_error(reply.payload, reply.payload_size, &err);
            byte_buf_consume(buf, reply.frame_size);
            if (err_parsed == UNISON_HANDSHAKE_OK) {
                snprintf(result.reason, sizeof(result.reason), "%s", err.detail);
            } else {
                snprintf(result.reason, sizeof(result.reason), "Handshake vom Server abgelehnt");
            }
            return result;
        }
        if (reply_type != UNISON_HS_MSG_SESSION_READY) {
            byte_buf_consume(buf, reply.frame_size);
            snprintf(result.reason, sizeof(result.reason), "unerwartete Antwort auf hello_ack");
            return result;
        }

        unison_session_ready ready;
        const unison_handshake_result ready_parsed =
            unison_parse_session_ready(reply.payload, reply.payload_size, &ready);
        byte_buf_consume(buf, reply.frame_size);
        if (ready_parsed != UNISON_HANDSHAKE_OK) {
            snprintf(result.reason, sizeof(result.reason), "session_ready konnte nicht gelesen werden");
            return result;
        }

        if (!ready.has_redirect) {
            result.width = (int32_t)ready.video.width;
            result.height = (int32_t)ready.video.height;
            strncpy(result.granted_video_mode, ready.video_mode, sizeof(result.granted_video_mode) - 1);
            result.granted_video_mode[sizeof(result.granted_video_mode) - 1] = '\0';
            result.has_video_port = ready.has_video_port;
            result.video_port = ready.video_port;
            result.ok = true;
            return result;
        }

        close(c->sockfd);
        c->sockfd = -1;
        byte_buf_free(buf);
        memset(buf, 0, sizeof(*buf));

        strncpy(c->host, ready.redirect_host, sizeof(c->host) - 1);
        c->host[sizeof(c->host) - 1] = '\0';
        c->port = ready.redirect_port;

        int new_fd = -1;
        if (!connect_and_ws_upgrade(c->host, c->port, &c->stop, &new_fd, buf)) {
            snprintf(result.reason, sizeof(result.reason), "Verbindung zum weitergeleiteten Port fehlgeschlagen");
            return result;
        }
        c->sockfd = new_fd;
    }

    snprintf(result.reason, sizeof(result.reason), "zu viele Weiterleitungen");
    return result;
}

static void handle_video_message(unison_native_client *c, const uint8_t *payload, size_t payload_size,
                                  uint8_t **inflate_out, size_t *inflate_out_cap, uint8_t **rgb565_out,
                                  size_t *rgb565_out_cap) {
    unison_video_header hdr;
    if (unison_parse_video_header(payload, payload_size, &hdr) != UNISON_OK) {
        return;
    }

    // UNISON_VIDEO_FORMAT_H264/_H265: hdr.compressed_data is a raw Annex-B
    // NAL stream straight from the server's encoder (see docs/protocol.md's
    // "Keyframe discipline"), not raw-deflate -- handed straight to Swift's
    // VideoToolbox decode path instead of through unison_inflate_raw()/
    // unison_decode_video_frame() below, same split jni_bridge.c's
    // handle_h264_h265_video_message() uses for MediaCodec.
    if (hdr.format & (UNISON_VIDEO_FORMAT_H264 | UNISON_VIDEO_FORMAT_H265)) {
        if (c->callbacks.on_compressed_video_frame) {
            c->callbacks.on_compressed_video_frame(
                c->callbacks.user_data, (int32_t)hdr.width, (int32_t)hdr.height,
                (hdr.format & UNISON_VIDEO_FORMAT_H265) ? 1 : 0, hdr.compressed_data, hdr.compressed_size);
        }
        return;
    }

    size_t inflate_needed = unison_video_max_inflated_size(hdr.width, hdr.height);
    if (inflate_needed > *inflate_out_cap) {
        uint8_t *grown = realloc(*inflate_out, inflate_needed);
        if (!grown) {
            return;
        }
        *inflate_out = grown;
        *inflate_out_cap = inflate_needed;
    }

    size_t framebuffer_needed = (size_t)hdr.width * hdr.height * 2;
    if (framebuffer_needed > *rgb565_out_cap) {
        uint8_t *grown = realloc(*rgb565_out, framebuffer_needed);
        if (!grown) {
            return;
        }
        *rgb565_out = grown;
        *rgb565_out_cap = framebuffer_needed;
    }

    size_t inflated_size = 0;
    if (unison_inflate_raw(hdr.compressed_data, hdr.compressed_size, *inflate_out, *inflate_out_cap,
                             &inflated_size) != UNISON_INFLATE_OK) {
        return;
    }
    if (unison_decode_video_frame(hdr.format, *inflate_out, inflated_size, hdr.width, hdr.height,
                                    *rgb565_out, *rgb565_out_cap) != UNISON_OK) {
        return;
    }

    if (c->callbacks.on_video_frame) {
        c->callbacks.on_video_frame(c->callbacks.user_data, (int32_t)hdr.width, (int32_t)hdr.height,
                                     *rgb565_out, framebuffer_needed);
    }
}

static void handle_audio_message(unison_native_client *c, const uint8_t *payload, size_t payload_size) {
    unison_audio_frame audio;
    if (unison_parse_audio_frame(payload, payload_size, &audio) != UNISON_OK || audio.sample_count == 0) {
        return;
    }

    int16_t *pcm = malloc(audio.sample_count * sizeof(int16_t));
    if (!pcm) {
        return;
    }
    for (size_t i = 0; i < audio.sample_count; i++) {
        pcm[i] = unison_read_s16le(audio.samples + i * 2);
    }

    if (c->callbacks.on_audio_frame) {
        c->callbacks.on_audio_frame(c->callbacks.user_data, (int32_t)audio.sample_rate,
                                     (int32_t)audio.channels, pcm, audio.sample_count);
    }
    free(pcm);
}

// Per-msg_type reassembly state for the UDP video/audio channel -- one
// instance each for video and audio in run_session_loop() below, since a
// frame_id counts independently per msg_type (unison_udp_fragment_header's
// own comment). Fragment i is always placed at byte offset
// i * UNISON_UDP_MAX_FRAGMENT_PAYLOAD regardless of arrival order, which
// is only correct because every fragment except the last is sent at
// EXACTLY that length -- see that macro's own comment. Same shape as
// jni_bridge.c's own udp_reassembly_state, but heap-growable (realloc)
// like this file's inflate_out/rgb565_out already are, rather than a
// fixed-size array -- this file has no equivalent size ceiling to bound
// against.
typedef struct {
    bool active;
    uint32_t frame_id;
    uint16_t fragment_count;
    uint16_t fragments_received;
    bool *received;        // fragment_count entries
    uint16_t *fragment_len; // fragment_count entries
    uint8_t *buf;           // fragment_count * UNISON_UDP_MAX_FRAGMENT_PAYLOAD bytes
    size_t buf_cap;         // in fragments, not bytes -- tracks received/fragment_len/buf's shared allocation size
} udp_reassembly_state;

static void udp_reassembly_free(udp_reassembly_state *state) {
    free(state->received);
    free(state->fragment_len);
    free(state->buf);
    memset(state, 0, sizeof(*state));
}

// Feeds one fragment into *state. A newer frame_id than the one currently
// in progress discards that old, incomplete state outright (same "newest
// wins" policy TILES dedup and Cemu/Android's own decode-backlog logic
// already use). Returns true (with *out_payload/*out_len valid, pointing
// into state->buf until the next call) once *state has every fragment for
// the frame_id currently being assembled; false otherwise (still
// incomplete, or this fragment was malformed and was ignored, or a
// realloc failed).
static bool process_udp_fragment(udp_reassembly_state *state, const unison_udp_fragment_header *hdr,
                                   const uint8_t *fragment_payload, size_t fragment_payload_len,
                                   const uint8_t **out_payload, size_t *out_len) {
    if (hdr->fragment_count == 0 || hdr->fragment_index >= hdr->fragment_count ||
        fragment_payload_len > UNISON_UDP_MAX_FRAGMENT_PAYLOAD) {
        return false;
    }
    if (hdr->fragment_index != (uint16_t)(hdr->fragment_count - 1) &&
        fragment_payload_len != UNISON_UDP_MAX_FRAGMENT_PAYLOAD) {
        return false;
    }

    if (!state->active || hdr->frame_id != state->frame_id || hdr->fragment_count != state->fragment_count) {
        if (hdr->fragment_count > state->buf_cap) {
            bool *new_received = realloc(state->received, hdr->fragment_count * sizeof(bool));
            uint16_t *new_len = realloc(state->fragment_len, hdr->fragment_count * sizeof(uint16_t));
            uint8_t *new_buf =
                realloc(state->buf, (size_t)hdr->fragment_count * UNISON_UDP_MAX_FRAGMENT_PAYLOAD);
            if (!new_received || !new_len || !new_buf) {
                free(new_received);
                free(new_len);
                free(new_buf);
                udp_reassembly_free(state);
                return false;
            }
            state->received = new_received;
            state->fragment_len = new_len;
            state->buf = new_buf;
            state->buf_cap = hdr->fragment_count;
        }
        state->active = true;
        state->frame_id = hdr->frame_id;
        state->fragment_count = hdr->fragment_count;
        state->fragments_received = 0;
        memset(state->received, 0, hdr->fragment_count * sizeof(bool));
        memset(state->fragment_len, 0, hdr->fragment_count * sizeof(uint16_t));
    }

    if (!state->received[hdr->fragment_index]) {
        state->received[hdr->fragment_index] = true;
        state->fragment_len[hdr->fragment_index] = (uint16_t)fragment_payload_len;
        memcpy(state->buf + (size_t)hdr->fragment_index * UNISON_UDP_MAX_FRAGMENT_PAYLOAD, fragment_payload,
               fragment_payload_len);
        state->fragments_received++;
    }

    if (state->fragments_received < state->fragment_count) {
        return false;
    }

    *out_len = (size_t)(state->fragment_count - 1) * UNISON_UDP_MAX_FRAGMENT_PAYLOAD +
               state->fragment_len[state->fragment_count - 1];
    *out_payload = state->buf;
    state->active = false; // consumed -- the next datagram (new frame_id) starts fresh
    return true;
}

// Parses one datagram off the UDP video/audio channel, reassembles it
// against the matching per-msg_type state, and dispatches a completed
// frame to handle_video_message()/handle_audio_message() -- same message-
// body handlers the WS-framed path uses, since the bodies are identical
// either way, only the framing around them differs.
static void process_udp_datagram(unison_native_client *c, udp_reassembly_state *video_state,
                                   udp_reassembly_state *audio_state, const uint8_t *datagram,
                                   size_t datagram_len, uint8_t **inflate_out, size_t *inflate_out_cap,
                                   uint8_t **rgb565_out, size_t *rgb565_out_cap) {
    unison_udp_fragment_header hdr;
    if (unison_parse_udp_fragment_header(datagram, datagram_len, &hdr) != UNISON_OK) {
        return;
    }
    const uint8_t *fragment_payload = datagram + UNISON_UDP_FRAGMENT_HEADER_SIZE;
    const size_t fragment_payload_len = datagram_len - UNISON_UDP_FRAGMENT_HEADER_SIZE;

    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    if (hdr.msg_type == UNISON_MSG_VIDEO &&
        process_udp_fragment(video_state, &hdr, fragment_payload, fragment_payload_len, &payload,
                              &payload_len)) {
        handle_video_message(c, payload, payload_len, inflate_out, inflate_out_cap, rgb565_out,
                              rgb565_out_cap);
    } else if (hdr.msg_type == UNISON_MSG_AUDIO &&
               process_udp_fragment(audio_state, &hdr, fragment_payload, fragment_payload_len, &payload,
                                     &payload_len)) {
        handle_audio_message(c, payload, payload_len);
    }
}

// Cemu's on-screen software keyboard (and any future server that does the
// same) is drawn as a host-side UI overlay, never part of the captured
// video -- this is the server telling the client to show its own native
// text input UI instead. req.text isn't NUL-terminated (it points straight
// into the WS payload buffer) -- passed through with an explicit length
// rather than handed to Swift as a C string, same "valid only for this
// call" contract as on_video_frame's rgb565.
static void handle_text_input_request_message(unison_native_client *c, const uint8_t *payload,
                                                size_t payload_size) {
    unison_text_input_request req;
    if (unison_parse_text_input_request(payload, payload_size, &req) != UNISON_OK) {
        return;
    }
    if (c->callbacks.on_text_input_request) {
        c->callbacks.on_text_input_request(c->callbacks.user_data, req.max_length, req.text, req.text_len);
    }
}

// Text-input counterpart to maybe_send_input/maybe_send_touch, but a
// one-shot send rather than "resend the latest state every time it
// changes" -- there's no ongoing state to resend, just a single response to
// whatever request handle_text_input_request_message() last delivered.
// Same shape as jni_bridge.c's own maybe_send_text_input_response().
static void maybe_send_text_input_response(unison_native_client *c) {
    if (!atomic_exchange(&c->text_response_dirty, false)) {
        return;
    }

    pthread_mutex_lock(&c->pending_text_response_mutex);
    char *text = c->pending_text_response_text;
    size_t text_len = c->pending_text_response_len;
    bool confirmed = c->pending_text_response_confirmed;
    c->pending_text_response_text = NULL;
    c->pending_text_response_len = 0;
    pthread_mutex_unlock(&c->pending_text_response_mutex);

    unison_text_input_response resp;
    resp.confirmed = confirmed ? 1 : 0;
    resp.text = text ? text : "";
    resp.text_len = text_len;

    const size_t payload_cap = unison_text_input_response_max_size(text_len);
    uint8_t *payload = malloc(payload_cap);
    if (payload) {
        size_t payload_len = unison_build_text_input_response(&resp, payload, payload_cap);
        if (payload_len > 0) {
            uint8_t mask_key[4];
            arc4random_buf(mask_key, sizeof(mask_key));

            const size_t frame_cap = unison_ws_build_frame_max_size(payload_len);
            uint8_t *frame_buf = malloc(frame_cap);
            if (frame_buf) {
                size_t frame_len = unison_ws_build_frame(UNISON_WS_OPCODE_BINARY, payload, payload_len,
                                                           mask_key, frame_buf, frame_cap);
                if (frame_len > 0) {
                    send_all(c->sockfd, frame_buf, frame_len, &c->stop);
                }
                free(frame_buf);
            }
        }
        free(payload);
    }
    free(text);
}

// Sends the current key mask, if it changed since the last send, as a
// masked WS input frame -- same "poll once per loop iteration, sole owner
// of the socket fd" reasoning as jni_bridge.c's own maybe_send_input().
static void maybe_send_input(unison_native_client *c) {
    if (!atomic_exchange(&c->input_dirty, false)) {
        return;
    }

    uint16_t mask = (uint16_t)atomic_load(&c->pending_keymask);
    uint8_t payload[UNISON_INPUT_FRAME_SIZE];
    unison_build_input_frame(mask, payload);

    uint8_t mask_key[4];
    arc4random_buf(mask_key, sizeof(mask_key));

    uint8_t frame_buf[UNISON_INPUT_FRAME_SIZE + 10];
    size_t frame_len = unison_ws_build_frame(UNISON_WS_OPCODE_BINARY, payload, sizeof(payload),
                                               mask_key, frame_buf, sizeof(frame_buf));
    if (frame_len > 0) {
        send_all(c->sockfd, frame_buf, frame_len, &c->stop);
    }
}

// Touch counterpart to maybe_send_input() above -- same "only on change,
// polled once per loop iteration" reasoning applies. Never called for a
// gba_buttons session since PlayerView only ever calls
// unison_native_send_touch() in touch mode, so touch_dirty simply never
// gets set there. Ported from jni_bridge.c's own maybe_send_touch(),
// same three-way frame-shape selection.
static void maybe_send_touch(unison_native_client *c) {
    if (!atomic_exchange(&c->touch_dirty, false)) {
        return;
    }

    const bool pressed = atomic_load(&c->pending_touch_pressed);
    uint8_t payload[UNISON_EXTENDED_INPUT_FRAME_SIZE];
    size_t payload_len;
    if (atomic_load(&c->extended_input)) {
        unison_extended_input input;
        input.pressed = pressed ? 1 : 0;
        input.touch_x = pressed ? (uint16_t)atomic_load(&c->pending_touch_x) : 0;
        input.touch_y = pressed ? (uint16_t)atomic_load(&c->pending_touch_y) : 0;
        input.buttons = (uint32_t)atomic_load(&c->pending_buttons);
        input.left_x = (int16_t)atomic_load(&c->pending_left_x);
        input.left_y = (int16_t)atomic_load(&c->pending_left_y);
        input.right_x = (int16_t)atomic_load(&c->pending_right_x);
        input.right_y = (int16_t)atomic_load(&c->pending_right_y);
        payload_len = unison_build_extended_input_frame(&input, payload);
    } else if (atomic_load(&c->has_buttons)) {
        unison_touch_and_buttons input;
        input.pressed = pressed ? 1 : 0;
        input.touch_x = pressed ? (uint16_t)atomic_load(&c->pending_touch_x) : 0;
        input.touch_y = pressed ? (uint16_t)atomic_load(&c->pending_touch_y) : 0;
        input.buttons = (uint32_t)atomic_load(&c->pending_buttons);
        payload_len = unison_build_touch_and_buttons_frame(&input, payload);
    } else {
        unison_touch_state touch;
        touch.pressed = pressed ? 1 : 0;
        touch.x = pressed ? (uint16_t)atomic_load(&c->pending_touch_x) : 0;
        touch.y = pressed ? (uint16_t)atomic_load(&c->pending_touch_y) : 0;
        payload_len = unison_build_touch_frame(&touch, payload);
    }

    uint8_t mask_key[4];
    arc4random_buf(mask_key, sizeof(mask_key));

    uint8_t frame_buf[UNISON_EXTENDED_INPUT_FRAME_SIZE + 10];
    size_t frame_len = unison_ws_build_frame(UNISON_WS_OPCODE_BINARY, payload, payload_len,
                                               mask_key, frame_buf, sizeof(frame_buf));
    if (frame_len > 0) {
        send_all(c->sockfd, frame_buf, frame_len, &c->stop);
    }
}

static void run_session_loop(unison_native_client *c, byte_buf *buf) {
    uint8_t chunk[4096];
    uint8_t udp_chunk[UNISON_UDP_MAX_DATAGRAM_SIZE];
    uint8_t *inflate_out = NULL;
    size_t inflate_out_cap = 0;
    uint8_t *rgb565_out = NULL;
    size_t rgb565_out_cap = 0;
    const bool has_video_conn = c->video_sockfd >= 0;
    udp_reassembly_state video_reassembly = {0};
    udp_reassembly_state audio_reassembly = {0};

    // The rendezvous handshake (udp_rendezvous(), client_thread_main) had
    // to read *some* datagram off the socket to know it succeeded -- that
    // datagram is real Video/Audio data, so it's processed here first,
    // before the poll loop below ever runs, rather than being discarded.
    if (has_video_conn && c->pending_first_video_datagram_len > 0) {
        process_udp_datagram(c, &video_reassembly, &audio_reassembly, c->pending_first_video_datagram,
                              (size_t)c->pending_first_video_datagram_len, &inflate_out, &inflate_out_cap,
                              &rgb565_out, &rgb565_out_cap);
        c->pending_first_video_datagram_len = 0;
    }

    while (!atomic_load(&c->stop)) {
        struct pollfd pfds[2];
        pfds[0] = (struct pollfd){.fd = c->sockfd, .events = POLLIN};
        nfds_t npfds = 1;
        if (has_video_conn) {
            pfds[1] = (struct pollfd){.fd = c->video_sockfd, .events = POLLIN};
            npfds = 2;
        }
        int pr = poll(pfds, npfds, 4); // short timeout: also need to notice pending input to send
        if (pr > 0 && (pfds[0].revents & POLLIN)) {
            ssize_t n = recv(c->sockfd, chunk, sizeof(chunk), 0);
            if (n <= 0) {
                break;
            }
            byte_buf_append(buf, chunk, (size_t)n);
        } else if (pr < 0 && errno != EINTR) {
            break;
        }
        if (has_video_conn && pr > 0 && (pfds[1].revents & POLLIN)) {
            // Unlike the control socket above, n <= 0 here is never
            // treated as "connection closed" -- UDP is connectionless, so
            // there is no such signal on this channel. Session lifetime
            // stays entirely driven by the control connection, per
            // docs/protocol.md.
            ssize_t n = recv(c->video_sockfd, udp_chunk, sizeof(udp_chunk), 0);
            if (n > 0) {
                process_udp_datagram(c, &video_reassembly, &audio_reassembly, udp_chunk, (size_t)n,
                                      &inflate_out, &inflate_out_cap, &rgb565_out, &rgb565_out_cap);
            }
        }

        bool should_stop = false;
        for (;;) {
            unison_ws_frame frame;
            unison_ws_frame_status fs = unison_ws_parse_frame(buf->data, buf->len, &frame);
            if (fs == UNISON_WS_FRAME_INCOMPLETE) {
                break;
            }
            if (fs == UNISON_WS_FRAME_ERR) {
                should_stop = true;
                break;
            }
            if (frame.opcode == UNISON_WS_OPCODE_CLOSE) {
                byte_buf_consume(buf, frame.frame_size);
                should_stop = true;
                break;
            }

            // Video/Audio normally arrive on the UDP channel instead once
            // has_video_conn -- either type landing here regardless (a
            // server bug, or one that doesn't honor its own
            // session_ready.video_port) is dropped rather than handled
            // twice/out of order against whatever the UDP reassembly
            // state is doing.
            unison_msg_type type;
            if (unison_peek_type(frame.payload, frame.payload_size, &type) == UNISON_OK) {
                if (type == UNISON_MSG_VIDEO) {
                    if (!has_video_conn) {
                        handle_video_message(c, frame.payload, frame.payload_size, &inflate_out,
                                             &inflate_out_cap, &rgb565_out, &rgb565_out_cap);
                    }
                } else if (type == UNISON_MSG_AUDIO) {
                    if (!has_video_conn) {
                        handle_audio_message(c, frame.payload, frame.payload_size);
                    }
                } else if (type == UNISON_MSG_TEXT_INPUT_REQUEST) {
                    handle_text_input_request_message(c, frame.payload, frame.payload_size);
                }
                // MIC_ENABLE: later phase, see this file's own top comment
                // -- simply not dispatched yet.
            }

            byte_buf_consume(buf, frame.frame_size);
        }
        if (should_stop) {
            break;
        }

        maybe_send_input(c);
        maybe_send_touch(c);
        maybe_send_text_input_response(c);
    }

    udp_reassembly_free(&video_reassembly);
    udp_reassembly_free(&audio_reassembly);
    free(inflate_out);
    free(rgb565_out);
}

static void *client_thread_main(void *arg) {
    unison_native_client *c = (unison_native_client *)arg;

    byte_buf buf = {0};
    int fd = -1;
    if (!connect_and_ws_upgrade(c->host, c->port, &c->stop, &fd, &buf)) {
        if (c->callbacks.on_disconnected) {
            c->callbacks.on_disconnected(c->callbacks.user_data, "Verbindung fehlgeschlagen");
        }
        byte_buf_free(&buf);
        return NULL;
    }
    c->sockfd = fd;

    app_handshake_result hs = perform_app_handshake(c, &buf);
    if (!hs.ok) {
        if (c->callbacks.on_disconnected) {
            c->callbacks.on_disconnected(c->callbacks.user_data, hs.reason);
        }
        byte_buf_free(&buf);
        if (c->sockfd >= 0) {
            close(c->sockfd);
        }
        return NULL;
    }

    atomic_store(&c->touch_input, hs.touch_input);
    atomic_store(&c->has_buttons, hs.has_buttons);
    atomic_store(&c->extended_input, hs.extended_input);

    // Dedicated video/audio channel (docs/protocol.md, "Dedicated
    // video/audio channel (UDP)", protocol_version 4): a connected UDP
    // socket carrying Video and Audio, no app handshake of its own (every
    // negotiated parameter already came from the exchange above) -- just
    // the rendezvous hello below. c->video_sockfd stays -1 (its calloc'd
    // default) when the server didn't offer video_port, which is exactly
    // what run_session_loop() already treats as "no video connection,
    // keep polling only the control socket."
    c->video_sockfd = -1;
    if (hs.has_video_port) {
        int video_fd = -1;
        if (!create_and_connect_udp_socket(c->host, hs.video_port, &video_fd)) {
            if (c->callbacks.on_disconnected) {
                c->callbacks.on_disconnected(c->callbacks.user_data, "Video-Verbindung fehlgeschlagen");
            }
            byte_buf_free(&buf);
            close(c->sockfd);
            return NULL;
        }
        ssize_t first_len = 0;
        if (!udp_rendezvous(video_fd, &c->stop, 5000, c->pending_first_video_datagram,
                            sizeof(c->pending_first_video_datagram), &first_len)) {
            if (c->callbacks.on_disconnected) {
                c->callbacks.on_disconnected(c->callbacks.user_data, "Video-Verbindung fehlgeschlagen");
            }
            byte_buf_free(&buf);
            close(video_fd);
            close(c->sockfd);
            return NULL;
        }
        c->video_sockfd = video_fd;
        c->pending_first_video_datagram_len = first_len;
    }

    if (c->callbacks.on_connected) {
        c->callbacks.on_connected(c->callbacks.user_data, hs.touch_input, hs.has_buttons,
                                   hs.extended_input, hs.width, hs.height, hs.granted_video_mode,
                                   hs.stream_type);
    }
    run_session_loop(c, &buf);
    byte_buf_free(&buf);

    if (c->callbacks.on_disconnected) {
        c->callbacks.on_disconnected(c->callbacks.user_data, "Verbindung getrennt");
    }
    close(c->sockfd);
    if (c->video_sockfd >= 0) {
        close(c->video_sockfd);
    }
    return NULL;
}

unison_native_client *unison_native_connect(const char *host, int port, const char *video_mode,
                                             unison_native_callbacks callbacks) {
    unison_native_client *c = calloc(1, sizeof(unison_native_client));
    if (!c) {
        return NULL;
    }

    strncpy(c->host, host, sizeof(c->host) - 1);
    strncpy(c->video_mode, video_mode, sizeof(c->video_mode) - 1);
    c->port = port;
    c->sockfd = -1;
    c->callbacks = callbacks;
    atomic_init(&c->stop, false);
    atomic_init(&c->pending_keymask, 0);
    atomic_init(&c->input_dirty, false);
    atomic_init(&c->touch_input, false);
    atomic_init(&c->has_buttons, false);
    atomic_init(&c->extended_input, false);
    atomic_init(&c->pending_touch_x, 0);
    atomic_init(&c->pending_touch_y, 0);
    atomic_init(&c->pending_touch_pressed, false);
    atomic_init(&c->touch_dirty, false);
    atomic_init(&c->pending_buttons, 0);
    atomic_init(&c->pending_left_x, 0);
    atomic_init(&c->pending_left_y, 0);
    atomic_init(&c->pending_right_x, 0);
    atomic_init(&c->pending_right_y, 0);
    pthread_mutex_init(&c->pending_text_response_mutex, NULL);
    c->pending_text_response_text = NULL;
    c->pending_text_response_len = 0;
    c->pending_text_response_confirmed = false;
    atomic_init(&c->text_response_dirty, false);

    if (pthread_create(&c->thread, NULL, client_thread_main, c) != 0) {
        pthread_mutex_destroy(&c->pending_text_response_mutex);
        free(c);
        return NULL;
    }
    return c;
}

void unison_native_send_input(unison_native_client *c, uint16_t keymask) {
    if (!c) {
        return;
    }
    atomic_store(&c->pending_keymask, keymask);
    atomic_store(&c->input_dirty, true);
}

void unison_native_send_touch(unison_native_client *c, int pressed, uint16_t x, uint16_t y) {
    if (!c) {
        return;
    }
    atomic_store(&c->pending_touch_pressed, pressed != 0);
    atomic_store(&c->pending_touch_x, x);
    atomic_store(&c->pending_touch_y, y);
    atomic_store(&c->touch_dirty, true);
}

void unison_native_send_extended_input(unison_native_client *c, uint32_t buttons, int16_t left_x,
                                        int16_t left_y, int16_t right_x, int16_t right_y) {
    if (!c) {
        return;
    }
    atomic_store(&c->pending_buttons, buttons);
    atomic_store(&c->pending_left_x, left_x);
    atomic_store(&c->pending_left_y, left_y);
    atomic_store(&c->pending_right_x, right_x);
    atomic_store(&c->pending_right_y, right_y);
    // Doesn't set touch_dirty itself -- rides along with whatever
    // unison_native_send_touch() next marks dirty, same as
    // jni_bridge.c's own nativeSendExtendedInput not being a send trigger
    // on its own (see this function's own header-comment on why).
}

// Only meaningful right after on_text_input_request() fires -- confirmed=0
// (the user cancelled) sends an empty text regardless of what text/text_len
// point at, matching unison_text_input_response's own convention that text
// is meaningless when not confirmed. Copies text before returning, unlike
// the fixed-size input setters above (nothing else here needs a copy since
// nothing else is variable-length).
void unison_native_send_text_input_response(unison_native_client *c, int confirmed, const char *text,
                                             size_t text_len) {
    if (!c) {
        return;
    }

    char *text_copy = NULL;
    if (confirmed && text_len > 0) {
        text_copy = malloc(text_len);
        if (text_copy) {
            memcpy(text_copy, text, text_len);
        } else {
            text_len = 0;
        }
    } else {
        text_len = 0;
    }

    pthread_mutex_lock(&c->pending_text_response_mutex);
    free(c->pending_text_response_text); // in case a previous response never got sent
    c->pending_text_response_text = text_copy;
    c->pending_text_response_len = text_len;
    c->pending_text_response_confirmed = confirmed != 0;
    pthread_mutex_unlock(&c->pending_text_response_mutex);
    atomic_store(&c->text_response_dirty, true);
}

void unison_native_disconnect(unison_native_client *c) {
    if (!c) {
        return;
    }
    atomic_store(&c->stop, true);
    pthread_join(c->thread, NULL);
    pthread_mutex_lock(&c->pending_text_response_mutex);
    free(c->pending_text_response_text);
    pthread_mutex_unlock(&c->pending_text_response_mutex);
    pthread_mutex_destroy(&c->pending_text_response_mutex);
    free(c);
}
