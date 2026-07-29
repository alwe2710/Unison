// JNI shell around finlink_core: owns the raw POSIX socket, runs the
// connect/handshake/receive loop on a background pthread, and calls back
// into Kotlin (GbaStreamClient.Listener) for video/audio/connection events.
// All protocol/codec logic (WS handshake+framing, message parsing, deflate)
// lives in core/ -- this file is deliberately "dumb": I/O and JNI plumbing
// only, mirroring the split already established between core/ and
// clients/<platform>/.

#include <android/log.h>
#include <errno.h>
#include <jni.h>
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

#include "finlink/endian.h"
#include "finlink/handshake.h"
#include "finlink/inflate.h"
#include "finlink/protocol.h"
#include "finlink/websocket.h"

#define LOG_TAG "finlink"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

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

typedef struct {
    JavaVM *jvm;
    jobject listener; // global ref
    char host[128];
    int port;
    int sockfd;
    pthread_t thread;
    atomic_bool stop;
    atomic_int pending_keymask;
    atomic_bool input_dirty;
    // Touch counterpart to pending_keymask/input_dirty above, used instead
    // of it whenever the handshake settled on touch_input (see
    // app_handshake_result) -- both pairs always exist on every session
    // since which one the Kotlin side ever actually calls (sendInput vs
    // sendTouch) is what determines which one carries real data; the other
    // just never gets marked dirty.
    atomic_int pending_touch_x;
    atomic_int pending_touch_y;
    atomic_bool pending_touch_pressed;
    atomic_bool touch_dirty;
    // Set once from app_handshake_result.extended_input right after a
    // successful handshake, read by maybe_send_touch() to pick which frame
    // shape/builder to use -- see that function's own comment. Buttons and
    // stick state below are meaningless (left at their init-time zero, only
    // ever written by nativeSendExtendedInput) on a plain touch_input
    // session that isn't also extended_input.
    atomic_bool extended_input;
    atomic_int pending_buttons;
    atomic_int pending_left_x;
    atomic_int pending_left_y;
    atomic_int pending_right_x;
    atomic_int pending_right_y;
    // Text input response (finlink/protocol.h's finlink_text_input_response)
    // -- unlike the continuously-resent state above, this is a one-shot
    // send: nativeSendTextInputResponse stashes the confirmed flag + a heap
    // copy of the UTF-8 text under pending_text_response_mutex (a plain
    // mutex rather than atomics, since the text itself needs to move, not
    // just a scalar), and text_response_dirty tells
    // maybe_send_text_input_response() there's one waiting -- same "only
    // the session thread touches the socket" reasoning as
    // maybe_send_input/maybe_send_touch.
    pthread_mutex_t pending_text_response_mutex;
    char *pending_text_response_text; // heap-owned, UTF-8, NOT NUL-terminated
    size_t pending_text_response_len;
    bool pending_text_response_confirmed;
    atomic_bool text_response_dirty;
    // Mic audio (finlink/protocol.h's FINLINK_MSG_MIC_AUDIO) -- unlike
    // pending_text_response above (a one-shot value), this is a continuous
    // FIFO byte queue: nativeSendMicAudio() (called repeatedly off the
    // Kotlin-managed AudioRecord capture thread) appends s16le sample bytes
    // under pending_mic_audio_mutex, and maybe_send_mic_audio() (network
    // thread, once per loop iteration) drains and sends whatever's
    // accumulated -- "latest wins" would drop audio, so this can't reuse
    // the atomic-scalar pattern maybe_send_input/maybe_send_touch use.
    // mic_enabled mirrors the most recent FINLINK_MSG_MIC_ENABLE from the
    // server (see handle_mic_enable_message) -- nativeSendMicAudio is a
    // harmless no-op while it's false, since Kotlin's own capture loop is
    // also gated on Listener.onMicEnable and shouldn't normally call it
    // then anyway, but a stray call racing a StopSampling() on the server
    // side is still possible.
    pthread_mutex_t pending_mic_audio_mutex;
    uint8_t *pending_mic_audio; // heap buffer of s16le sample bytes
    size_t pending_mic_audio_len;
    size_t pending_mic_audio_cap;
    uint32_t pending_mic_sample_rate;
    atomic_bool mic_enabled;
} finlink_session;

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
// perform_app_handshake below for that). Takes host/port as parameters
// rather than reading them off `s` so it can be called again against a
// redirect target without disturbing s->host/s->port until it's known to
// have succeeded. On success, *out_fd is the connected socket and `leftover`
// holds any bytes received past the handshake response header -- those are
// already WebSocket frame data (the server sends `hello` immediately after
// upgrading, so this is often non-empty) and must feed straight into the
// caller's receive buffer, not be discarded.
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
        LOGE("getaddrinfo failed for %s:%d", host, port);
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
        LOGE("connect failed for %s:%d", host, port);
        return false;
    }

    uint8_t random_bytes[16];
    arc4random_buf(random_bytes, sizeof(random_bytes));
    char key[FINLINK_WS_KEY_BUF_LEN];
    finlink_ws_generate_key(random_bytes, key);

    char host_header[160];
    snprintf(host_header, sizeof(host_header), "%s:%d", host, port);

    char request[512];
    size_t request_len =
        finlink_ws_build_handshake_request(host_header, "/", key, request, sizeof(request));
    if (request_len == 0 || !send_all(fd, (const uint8_t *)request, request_len, stop_flag)) {
        LOGE("failed to send handshake request");
        close(fd);
        return false;
    }

    byte_buf recv_buf = {0};
    uint8_t chunk[1024];
    finlink_ws_handshake_status status = FINLINK_WS_HANDSHAKE_INCOMPLETE;
    size_t header_len = 0;

    while (status == FINLINK_WS_HANDSHAKE_INCOMPLETE) {
        if (atomic_load(stop_flag)) {
            byte_buf_free(&recv_buf);
            close(fd);
            return false;
        }
        ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            LOGE("connection closed during handshake");
            byte_buf_free(&recv_buf);
            close(fd);
            return false;
        }
        byte_buf_append(&recv_buf, chunk, (size_t)n);
        if (recv_buf.len > 16384) { // guard against a runaway/malformed response
            LOGE("handshake response too large");
            byte_buf_free(&recv_buf);
            close(fd);
            return false;
        }
        status = finlink_ws_parse_handshake_response(recv_buf.data, recv_buf.len, key, &header_len);
        if (status == FINLINK_WS_HANDSHAKE_ERR) {
            LOGE("handshake rejected (bad status or Sec-WebSocket-Accept mismatch)");
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

// Blocks (bounded by timeout_ms, checking s->stop throughout) until `buf`
// holds one full WebSocket frame, receiving more off s->sockfd as needed.
// Frame data already sitting in `buf` (e.g. left over from the WS upgrade,
// or a previous call) is tried first before any recv().
//
// Deliberately does NOT consume the frame's bytes from `buf` on success --
// out_frame->payload points *into* buf->data, and byte_buf_consume() shifts
// buf->data's contents down in place (memmove), which would invalidate that
// pointer out from under the caller before it ever reads it. The caller
// must finish reading out_frame->payload first, then call
// byte_buf_consume(buf, out_frame->frame_size) itself once it's safe to
// discard -- same order run_session_loop's own frame handling already uses
// for Video/Audio/Input frames.
static bool receive_one_ws_frame(finlink_session *s, byte_buf *buf, finlink_ws_frame *out_frame,
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
        finlink_ws_frame_status fs = finlink_ws_parse_frame(buf->data, buf->len, out_frame);
        if (fs == FINLINK_WS_FRAME_OK) {
            return true;
        }
        if (fs == FINLINK_WS_FRAME_ERR) {
            LOGE("malformed frame while waiting for handshake message");
            return false;
        }

        if (atomic_load(&s->stop)) {
            return false;
        }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long remaining_ms = (long)(deadline.tv_sec - now.tv_sec) * 1000 +
                             (deadline.tv_nsec - now.tv_nsec) / 1000000;
        if (remaining_ms <= 0) {
            LOGE("timed out waiting for handshake message");
            return false;
        }

        struct pollfd pfd = {.fd = s->sockfd, .events = POLLIN};
        int pr = poll(&pfd, 1, remaining_ms > 200 ? 200 : (int)remaining_ms);
        if (pr < 0 && errno != EINTR) {
            return false;
        }
        if (pr > 0 && (pfd.revents & POLLIN)) {
            uint8_t chunk[4096];
            ssize_t n = recv(s->sockfd, chunk, sizeof(chunk), 0);
            if (n <= 0) {
                LOGE("connection closed while waiting for handshake message");
                return false;
            }
            byte_buf_append(buf, chunk, (size_t)n);
        }
    }
}

// GC_GBA_LINK player ports are always this + the GC device number
// (docs/protocol.md; matches GbaStreamClient.PLAYER_BASE_PORT on the Kotlin
// side and GBA_STREAM_PLAYER_BASE_PORT in the dolphin-gba-stream fork).
#define FINLINK_GBA_LINK_PLAYER_BASE_PORT 6801

#define APP_HANDSHAKE_TIMEOUT_MS 3000

typedef struct {
    bool ok;
    char reason[160];
    // Which client->server input shape to use for this session -- derived
    // from the server's own hello.input_encoding (protocol.h), not guessed
    // client-side, since manual host entry has no beacon to read a
    // stream_type from beforehand and a redirect hop can in principle land
    // on a different stream type than the one first dialed. true for either
    // touch encoding below, false for "gba_buttons".
    bool touch_input;
    // Distinguishes the two touch encodings once touch_input is true:
    // "n3ds_touch_and_buttons" (buttons + circle pad/analog sticks
    // remotely controllable too, not just touch -- currently only Azahar's
    // N3DS_BOTTOM_SCREEN advertises this) vs. the older, narrower
    // "n3ds_touch" (touch only, buttons/circle-pad stay local -- still
    // what Cemu/melonDS advertise until they get the same treatment).
    // Meaningless when touch_input is false.
    bool extended_input;
} app_handshake_result;

// App-level handshake (finlink/handshake.h, docs/protocol.md
// "Verbindungsaufbau: Handshake"), run once `s->sockfd` is already
// WebSocket-upgraded and `buf` may already hold the server's first message.
// On a `session_ready` with `redirect` (only possible for multi-slot stream
// types via the lobby port -- not reachable through this app's current
// "connect straight to a player port" UI, but handled anyway per
// docs/protocol.md rather than assumed away), closes the current socket,
// reconnects to the redirect target, and repeats -- bounded to one hop,
// matching the protocol's own design (never more than a single redirect).
static app_handshake_result perform_app_handshake(finlink_session *s, byte_buf *buf) {
    app_handshake_result result = {false, ""};

    for (int hop = 0; hop < 2; hop++) {
        finlink_ws_frame frame;
        if (!receive_one_ws_frame(s, buf, &frame, APP_HANDSHAKE_TIMEOUT_MS)) {
            snprintf(result.reason, sizeof(result.reason),
                     "Server hat keinen Handshake gestartet (evtl. veraltete Protokollversion)");
            return result;
        }
        if (frame.opcode != FINLINK_WS_OPCODE_TEXT ||
            finlink_peek_handshake_message(frame.payload, frame.payload_size) != FINLINK_HS_MSG_HELLO) {
            snprintf(result.reason, sizeof(result.reason), "Unerwartete erste Nachricht vom Server");
            return result;
        }

        finlink_hello hello;
        const finlink_handshake_result hello_parsed =
            finlink_parse_hello(frame.payload, frame.payload_size, &hello);
        // Done reading frame.payload either way -- safe to drop it from buf
        // now, before it's invalidated by any later receive_one_ws_frame()
        // call shifting buf's contents (see that function's own comment).
        byte_buf_consume(buf, frame.frame_size);
        if (hello_parsed != FINLINK_HANDSHAKE_OK) {
            snprintf(result.reason, sizeof(result.reason), "hello konnte nicht gelesen werden");
            return result;
        }
        if (hello.protocol_version != FINLINK_PROTOCOL_VERSION) {
            snprintf(result.reason, sizeof(result.reason),
                     "Server spricht Protokollversion %d, dieser Client unterstuetzt nur Version %d "
                     "-- bitte Client oder Server aktualisieren",
                     hello.protocol_version, FINLINK_PROTOCOL_VERSION);
            return result;
        }
        result.extended_input = strcmp(hello.input_encoding, "n3ds_touch_and_buttons") == 0;
        result.touch_input =
            result.extended_input || strcmp(hello.input_encoding, "n3ds_touch") == 0;

        finlink_hello_ack_request ack_req;
        memset(&ack_req, 0, sizeof(ack_req));
        // GC_GBA_LINK is the one stream type this app ever dials a specific
        // already-chosen player port for (see GbaStreamClient.PLAYER_BASE_PORT
        // / MenuActivity's picker) -- there, the slot being asked about is
        // simply "the one this connection is on", derived the same way the
        // server itself assigns device_number to player ports
        // (GBA_STREAM_PLAYER_BASE_PORT + device_number). Every other stream
        // type is single-client and connects straight to the beacon's
        // handshake_port instead (MenuActivity, discovered-entry tap) -- that
        // port has nothing to do with player-port numbering, so the same
        // subtraction there produced a garbage out-of-range slot (e.g. 9 for
        // Azahar's default port 6810), which the server rejected outright,
        // dropping the connection before any video frame could ever arrive.
        ack_req.requested_slot = strcmp(hello.stream_type, "GC_GBA_LINK") == 0
                                      ? s->port - FINLINK_GBA_LINK_PLAYER_BASE_PORT
                                      : 0;
        // Generous/native limits throughout: a phone has no trouble with a
        // 240x160 GBA stream at native rate, so there's never a reason for
        // this client to ask the server to downscale.
        ack_req.max_width = hello.video.width > 0 ? hello.video.width : 240;
        ack_req.max_height = hello.video.height > 0 ? hello.video.height : 160;
        ack_req.max_fps = hello.video.fps > 0 ? hello.video.fps : 60.0;
        ack_req.wants_audio = hello.has_audio;
        ack_req.max_sample_rate = hello.has_audio && hello.audio.sample_rate > 0 ? hello.audio.sample_rate : 48000;
        ack_req.max_channels = hello.has_audio && hello.audio.channels > 0 ? hello.audio.channels : 2;

        char ack_json[512];
        size_t ack_len = finlink_build_hello_ack(&ack_req, ack_json, sizeof(ack_json));
        if (ack_len == 0) {
            snprintf(result.reason, sizeof(result.reason), "hello_ack zu gross fuer den Puffer");
            return result;
        }

        uint8_t mask_key[4];
        arc4random_buf(mask_key, sizeof(mask_key));
        uint8_t frame_buf[512 + 14];
        size_t frame_len = finlink_ws_build_frame(FINLINK_WS_OPCODE_TEXT, (const uint8_t *)ack_json,
                                                   ack_len, mask_key, frame_buf, sizeof(frame_buf));
        if (frame_len == 0 || !send_all(s->sockfd, frame_buf, frame_len, &s->stop)) {
            snprintf(result.reason, sizeof(result.reason), "hello_ack konnte nicht gesendet werden");
            return result;
        }

        finlink_ws_frame reply;
        if (!receive_one_ws_frame(s, buf, &reply, APP_HANDSHAKE_TIMEOUT_MS)) {
            snprintf(result.reason, sizeof(result.reason), "keine Antwort auf hello_ack");
            return result;
        }
        if (reply.opcode != FINLINK_WS_OPCODE_TEXT) {
            LOGE("unexpected opcode after hello_ack: 0x%x (payload_size=%zu)", reply.opcode,
                 reply.payload_size);
            snprintf(result.reason, sizeof(result.reason), "unerwartete Antwort auf hello_ack");
            return result;
        }

        const finlink_handshake_message_type reply_type =
            finlink_peek_handshake_message(reply.payload, reply.payload_size);
        if (reply_type == FINLINK_HS_MSG_HANDSHAKE_ERROR) {
            finlink_handshake_error err;
            const finlink_handshake_result err_parsed =
                finlink_parse_handshake_error(reply.payload, reply.payload_size, &err);
            byte_buf_consume(buf, reply.frame_size); // done reading reply.payload either way
            if (err_parsed == FINLINK_HANDSHAKE_OK) {
                snprintf(result.reason, sizeof(result.reason), "%s", err.detail);
            } else {
                snprintf(result.reason, sizeof(result.reason), "Handshake vom Server abgelehnt");
            }
            return result;
        }
        if (reply_type != FINLINK_HS_MSG_SESSION_READY) {
            // Logs the raw payload (bounded, and JSON text is never NUL-safe
            // to assume, so an explicit length-bounded %.*s) -- this is the
            // one report we have of this path actually firing ("ab und zu"),
            // and guessing at the cause without seeing what the server
            // actually sent isn't worth another guess-and-ship round trip.
            LOGE("unexpected message after hello_ack (peeked type=%d): %.*s", (int)reply_type,
                 (int)(reply.payload_size < 400 ? reply.payload_size : 400), (const char *)reply.payload);
            byte_buf_consume(buf, reply.frame_size);
            snprintf(result.reason, sizeof(result.reason), "unerwartete Antwort auf hello_ack");
            return result;
        }

        finlink_session_ready ready;
        const finlink_handshake_result ready_parsed =
            finlink_parse_session_ready(reply.payload, reply.payload_size, &ready);
        // Done reading reply.payload either way -- must happen before any
        // later use of `buf` (the redirect reconnect below reuses it, and a
        // successful non-redirect return hands it to run_session_loop),
        // same reasoning as the `hello` frame's consume above.
        byte_buf_consume(buf, reply.frame_size);
        if (ready_parsed != FINLINK_HANDSHAKE_OK) {
            snprintf(result.reason, sizeof(result.reason), "session_ready konnte nicht gelesen werden");
            return result;
        }

        if (!ready.has_redirect) {
            result.ok = true;
            return result;
        }

        // Redirect: this connection carries no stream data, ever -- close
        // it, reconnect to the target, and let the loop try the same
        // exchange again there (hop 1, the only one this loop allows).
        close(s->sockfd);
        s->sockfd = -1;
        byte_buf_free(buf);
        memset(buf, 0, sizeof(*buf));

        strncpy(s->host, ready.redirect_host, sizeof(s->host) - 1);
        s->host[sizeof(s->host) - 1] = '\0';
        s->port = ready.redirect_port;

        int new_fd = -1;
        if (!connect_and_ws_upgrade(s->host, s->port, &s->stop, &new_fd, buf)) {
            snprintf(result.reason, sizeof(result.reason), "Verbindung zum weitergeleiteten Port fehlgeschlagen");
            return result;
        }
        s->sockfd = new_fd;
        // loop: expect a fresh `hello` on the new connection
    }

    snprintf(result.reason, sizeof(result.reason), "zu viele Weiterleitungen");
    return result;
}

// `inflate_out`/`inflate_out_cap` is scratch space for finlink_inflate_raw()'s
// output, whose content depends on hdr.format (raw/indexed pixels, whole
// frame or only changed tiles -- see finlink/protocol.h) --
// `rgb565_out`/`rgb565_out_cap` is the PERSISTENT framebuffer, decoded from
// that via finlink_decode_video_frame(): width*height RGB565 pixels,
// row-major. It's only reallocated when growing (never shrunk, never
// cleared), so its content survives across calls at a fixed size -- which
// is exactly what a FINLINK_VIDEO_FORMAT_TILES message needs, since it only
// patches the tiles it lists and leaves every other pixel as the previous
// frame decoded it.
static void handle_video_message(JNIEnv *env, finlink_session *s, jmethodID on_video,
                                  const uint8_t *payload, size_t payload_size, uint8_t **inflate_out,
                                  size_t *inflate_out_cap, uint8_t **rgb565_out, size_t *rgb565_out_cap) {
    finlink_video_header hdr;
    if (finlink_parse_video_header(payload, payload_size, &hdr) != FINLINK_OK) {
        return;
    }

    size_t inflate_needed = finlink_video_max_inflated_size(hdr.width, hdr.height);
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
    if (finlink_inflate_raw(hdr.compressed_data, hdr.compressed_size, *inflate_out, *inflate_out_cap,
                             &inflated_size) != FINLINK_INFLATE_OK) {
        return;
    }
    if (finlink_decode_video_frame(hdr.format, *inflate_out, inflated_size, hdr.width, hdr.height, *rgb565_out,
                                    *rgb565_out_cap) != FINLINK_OK) {
        return;
    }

    jbyteArray arr = (*env)->NewByteArray(env, (jsize)framebuffer_needed);
    (*env)->SetByteArrayRegion(env, arr, 0, (jsize)framebuffer_needed, (const jbyte *)*rgb565_out);
    (*env)->CallVoidMethod(env, s->listener, on_video, (jint)hdr.width, (jint)hdr.height, arr);
    (*env)->DeleteLocalRef(env, arr);
}

static void handle_audio_message(JNIEnv *env, finlink_session *s, jmethodID on_audio,
                                  const uint8_t *payload, size_t payload_size) {
    finlink_audio_frame audio;
    if (finlink_parse_audio_frame(payload, payload_size, &audio) != FINLINK_OK ||
        audio.sample_count == 0) {
        return;
    }

    jshort *tmp = malloc(audio.sample_count * sizeof(jshort));
    if (!tmp) {
        return;
    }
    for (size_t i = 0; i < audio.sample_count; i++) {
        tmp[i] = (jshort)finlink_read_s16le(audio.samples + i * 2);
    }

    jshortArray arr = (*env)->NewShortArray(env, (jsize)audio.sample_count);
    (*env)->SetShortArrayRegion(env, arr, 0, (jsize)audio.sample_count, tmp);
    free(tmp);

    (*env)->CallVoidMethod(env, s->listener, on_audio, (jint)audio.sample_rate, (jint)audio.channels,
                            arr);
    (*env)->DeleteLocalRef(env, arr);
}

// Cemu's on-screen software keyboard (and any future server that does the
// same) is drawn as a host-side UI overlay, never part of the captured
// video -- this is the server telling the client to show its own native
// text input UI instead. req.text isn't NUL-terminated (it points straight
// into the WS payload buffer), so it needs a temporary NUL-terminated copy
// before NewStringUTF() can use it.
static void handle_text_input_request_message(JNIEnv *env, finlink_session *s,
                                                jmethodID on_text_input_request, const uint8_t *payload,
                                                size_t payload_size) {
    finlink_text_input_request req;
    if (finlink_parse_text_input_request(payload, payload_size, &req) != FINLINK_OK) {
        return;
    }
    char *text_nul = malloc(req.text_len + 1);
    if (!text_nul) {
        return;
    }
    memcpy(text_nul, req.text, req.text_len);
    text_nul[req.text_len] = '\0';
    jstring jtext = (*env)->NewStringUTF(env, text_nul);
    free(text_nul);
    (*env)->CallVoidMethod(env, s->listener, on_text_input_request, (jint)req.max_length, jtext);
    (*env)->DeleteLocalRef(env, jtext);
}

// Mirrors real mic hardware: the console only wants microphone input while
// a game has it powered on and actively sampling (see e.g. 3DS's mic:u
// service), not continuously just because a stream is connected -- this
// tells Kotlin's Listener to start or stop its own AudioRecord capture
// loop accordingly, at the sample rate the console actually asked for.
static void handle_mic_enable_message(JNIEnv *env, finlink_session *s, jmethodID on_mic_enable,
                                        const uint8_t *payload, size_t payload_size) {
    finlink_mic_enable enable;
    if (finlink_parse_mic_enable_frame(payload, payload_size, &enable) != FINLINK_OK) {
        return;
    }
    atomic_store(&s->mic_enabled, enable.enabled != 0);
    (*env)->CallVoidMethod(env, s->listener, on_mic_enable, (jboolean)(enable.enabled != 0),
                            (jint)enable.sample_rate);
}

// Text-input counterpart to maybe_send_input/maybe_send_touch, but a
// one-shot send rather than "resend the latest state every time it
// changes": there's no ongoing state to resend, just a single response to
// whatever request handle_text_input_request_message() last delivered.
// Dynamically allocated (unlike those two's fixed-size stack buffers)
// since the text length is caller-controlled, not a small fixed shape.
static void maybe_send_text_input_response(finlink_session *s) {
    if (!atomic_exchange(&s->text_response_dirty, false)) {
        return;
    }

    pthread_mutex_lock(&s->pending_text_response_mutex);
    char *text = s->pending_text_response_text;
    size_t text_len = s->pending_text_response_len;
    bool confirmed = s->pending_text_response_confirmed;
    s->pending_text_response_text = NULL;
    s->pending_text_response_len = 0;
    pthread_mutex_unlock(&s->pending_text_response_mutex);

    finlink_text_input_response resp;
    resp.confirmed = confirmed ? 1 : 0;
    resp.text = text ? text : "";
    resp.text_len = text_len;

    const size_t payload_cap = finlink_text_input_response_max_size(text_len);
    uint8_t *payload = malloc(payload_cap);
    if (payload) {
        size_t payload_len = finlink_build_text_input_response(&resp, payload, payload_cap);
        if (payload_len > 0) {
            uint8_t mask_key[4];
            arc4random_buf(mask_key, sizeof(mask_key));

            const size_t frame_cap = finlink_ws_build_frame_max_size(payload_len);
            uint8_t *frame_buf = malloc(frame_cap);
            if (frame_buf) {
                size_t frame_len = finlink_ws_build_frame(FINLINK_WS_OPCODE_BINARY, payload, payload_len,
                                                           mask_key, frame_buf, frame_cap);
                if (frame_len > 0) {
                    send_all(s->sockfd, frame_buf, frame_len, &s->stop);
                }
                free(frame_buf);
            }
        }
        free(payload);
    }
    free(text);
}

// Mic-audio counterpart to maybe_send_text_input_response, but draining a
// FIFO byte queue rather than taking a single one-shot value -- see
// pending_mic_audio's own comment (finlink_session) for why this can't
// reuse the "latest wins" pattern maybe_send_input/maybe_send_touch use.
// Hand-built the same way Cemu's WiiuGamepadStream::SendAudioFrame() and
// this app's own FINLINK_MSG_AUDIO receive side are -- no shared
// finlink_build_mic_audio_frame() in core since, like FINLINK_MSG_AUDIO,
// there's exactly one implementation producing this message right now.
static void maybe_send_mic_audio(finlink_session *s) {
    pthread_mutex_lock(&s->pending_mic_audio_mutex);
    if (s->pending_mic_audio_len == 0) {
        pthread_mutex_unlock(&s->pending_mic_audio_mutex);
        return;
    }
    uint8_t *samples = s->pending_mic_audio;
    size_t samples_len = s->pending_mic_audio_len;
    uint32_t sample_rate = s->pending_mic_sample_rate;
    s->pending_mic_audio = NULL;
    s->pending_mic_audio_len = 0;
    s->pending_mic_audio_cap = 0;
    pthread_mutex_unlock(&s->pending_mic_audio_mutex);

    const size_t payload_len = 6 + samples_len; // type(1) + sample_rate(4) + channels(1)
    uint8_t *payload = malloc(payload_len);
    if (payload) {
        payload[0] = FINLINK_MSG_MIC_AUDIO;
        finlink_write_u32le(payload + 1, sample_rate);
        payload[5] = 1; // mono -- the only channel count a mic input ever has here
        memcpy(payload + 6, samples, samples_len);

        uint8_t mask_key[4];
        arc4random_buf(mask_key, sizeof(mask_key));
        const size_t frame_cap = finlink_ws_build_frame_max_size(payload_len);
        uint8_t *frame_buf = malloc(frame_cap);
        if (frame_buf) {
            size_t frame_len = finlink_ws_build_frame(FINLINK_WS_OPCODE_BINARY, payload, payload_len,
                                                       mask_key, frame_buf, frame_cap);
            if (frame_len > 0) {
                send_all(s->sockfd, frame_buf, frame_len, &s->stop);
            }
            free(frame_buf);
        }
        free(payload);
    }
    free(samples);
}

// Sends the current key mask, if it changed since the last send, as a
// masked WS input frame. Called once per loop iteration rather than
// eagerly from nativeSendInput, so this thread stays the sole owner of the
// socket fd -- no send-side locking needed.
static void maybe_send_input(finlink_session *s) {
    if (!atomic_exchange(&s->input_dirty, false)) {
        return;
    }

    uint16_t mask = (uint16_t)atomic_load(&s->pending_keymask);
    uint8_t payload[FINLINK_INPUT_FRAME_SIZE];
    finlink_build_input_frame(mask, payload);

    uint8_t mask_key[4];
    arc4random_buf(mask_key, sizeof(mask_key));

    uint8_t frame_buf[FINLINK_INPUT_FRAME_SIZE + 10];
    size_t frame_len = finlink_ws_build_frame(FINLINK_WS_OPCODE_BINARY, payload, sizeof(payload),
                                               mask_key, frame_buf, sizeof(frame_buf));
    if (frame_len > 0) {
        send_all(s->sockfd, frame_buf, frame_len, &s->stop);
    }
}

// Touch counterpart to maybe_send_input() above -- same "only on change,
// polled once per loop iteration" reasoning applies. Never called for a
// gba_buttons session since PlayerActivity only ever calls sendTouch() in
// touch mode, so touch_dirty simply never gets set there.
static void maybe_send_touch(finlink_session *s) {
    if (!atomic_exchange(&s->touch_dirty, false)) {
        return;
    }

    const bool pressed = atomic_load(&s->pending_touch_pressed);
    // x/y (and, for an extended session, buttons/sticks) are meaningless on
    // release for touch specifically (finlink_touch_state's own comment,
    // protocol.h) -- pending_touch_x/y are left at whatever they last held
    // rather than reset on release, so reading them unconditionally would
    // be fine, but zeroing them out here actually honors the wire
    // convention rather than sending stale coordinates the receiver is
    // supposed to ignore. Buttons/sticks are NOT gated on touch's pressed
    // state the same way -- e.g. holding a button with no finger on the
    // touch area at all is a real, valid, independent input -- so those are
    // always read from whatever nativeSendExtendedInput last set.
    uint8_t payload[FINLINK_EXTENDED_INPUT_FRAME_SIZE];
    size_t payload_len;
    if (atomic_load(&s->extended_input)) {
        finlink_extended_input input;
        input.pressed = pressed ? 1 : 0;
        input.touch_x = pressed ? (uint16_t)atomic_load(&s->pending_touch_x) : 0;
        input.touch_y = pressed ? (uint16_t)atomic_load(&s->pending_touch_y) : 0;
        input.buttons = (uint32_t)atomic_load(&s->pending_buttons);
        input.left_x = (int16_t)atomic_load(&s->pending_left_x);
        input.left_y = (int16_t)atomic_load(&s->pending_left_y);
        input.right_x = (int16_t)atomic_load(&s->pending_right_x);
        input.right_y = (int16_t)atomic_load(&s->pending_right_y);
        payload_len = finlink_build_extended_input_frame(&input, payload);
    } else {
        finlink_touch_state touch;
        touch.pressed = pressed ? 1 : 0;
        touch.x = pressed ? (uint16_t)atomic_load(&s->pending_touch_x) : 0;
        touch.y = pressed ? (uint16_t)atomic_load(&s->pending_touch_y) : 0;
        payload_len = finlink_build_touch_frame(&touch, payload);
    }

    uint8_t mask_key[4];
    arc4random_buf(mask_key, sizeof(mask_key));

    uint8_t frame_buf[FINLINK_EXTENDED_INPUT_FRAME_SIZE + 10];
    size_t frame_len = finlink_ws_build_frame(FINLINK_WS_OPCODE_BINARY, payload, payload_len,
                                               mask_key, frame_buf, sizeof(frame_buf));
    if (frame_len > 0) {
        send_all(s->sockfd, frame_buf, frame_len, &s->stop);
    }
}

static void run_session_loop(JNIEnv *env, finlink_session *s, jmethodID on_video, jmethodID on_audio,
                              jmethodID on_text_input_request, jmethodID on_mic_enable, byte_buf *buf) {
    uint8_t chunk[4096];
    uint8_t *inflate_out = NULL;
    size_t inflate_out_cap = 0;
    uint8_t *rgb565_out = NULL;
    size_t rgb565_out_cap = 0;

    while (!atomic_load(&s->stop)) {
        struct pollfd pfd = {.fd = s->sockfd, .events = POLLIN};
        int pr = poll(&pfd, 1, 4); // short timeout: also need to notice pending input to send
        if (pr > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = recv(s->sockfd, chunk, sizeof(chunk), 0);
            if (n <= 0) {
                break; // peer closed or socket error
            }
            byte_buf_append(buf, chunk, (size_t)n);
        } else if (pr < 0 && errno != EINTR) {
            break;
        }

        bool should_stop = false;
        for (;;) {
            finlink_ws_frame frame;
            finlink_ws_frame_status fs = finlink_ws_parse_frame(buf->data, buf->len, &frame);
            if (fs == FINLINK_WS_FRAME_INCOMPLETE) {
                break;
            }
            if (fs == FINLINK_WS_FRAME_ERR) {
                should_stop = true;
                break;
            }

            if (frame.opcode == FINLINK_WS_OPCODE_CLOSE) {
                byte_buf_consume(buf, frame.frame_size);
                should_stop = true; // not an error, but reuse the same "stop outer loop" path
                break;
            }

            finlink_msg_type type;
            if (finlink_peek_type(frame.payload, frame.payload_size, &type) == FINLINK_OK) {
                if (type == FINLINK_MSG_VIDEO) {
                    handle_video_message(env, s, on_video, frame.payload, frame.payload_size,
                                         &inflate_out, &inflate_out_cap, &rgb565_out, &rgb565_out_cap);
                } else if (type == FINLINK_MSG_AUDIO) {
                    handle_audio_message(env, s, on_audio, frame.payload, frame.payload_size);
                } else if (type == FINLINK_MSG_TEXT_INPUT_REQUEST) {
                    handle_text_input_request_message(env, s, on_text_input_request, frame.payload,
                                                       frame.payload_size);
                } else if (type == FINLINK_MSG_MIC_ENABLE) {
                    handle_mic_enable_message(env, s, on_mic_enable, frame.payload, frame.payload_size);
                }
            }

            byte_buf_consume(buf, frame.frame_size);
        }
        if (should_stop) {
            break;
        }

        maybe_send_input(s);
        maybe_send_touch(s);
        maybe_send_text_input_response(s);
        maybe_send_mic_audio(s);
    }

    free(inflate_out);
    free(rgb565_out);
}

static void call_on_disconnected(JNIEnv *env, finlink_session *s, jmethodID on_disconnected,
                                  const char *reason) {
    jstring jreason = (*env)->NewStringUTF(env, reason);
    (*env)->CallVoidMethod(env, s->listener, on_disconnected, jreason);
    (*env)->DeleteLocalRef(env, jreason);
}

static void *client_thread_main(void *arg) {
    finlink_session *s = (finlink_session *)arg;
    JNIEnv *env = NULL;
    (*s->jvm)->AttachCurrentThread(s->jvm, &env, NULL);

    jclass listener_class = (*env)->GetObjectClass(env, s->listener);
    jmethodID on_connected = (*env)->GetMethodID(env, listener_class, "onConnected", "(ZZ)V");
    jmethodID on_video = (*env)->GetMethodID(env, listener_class, "onVideoFrame", "(II[B)V");
    jmethodID on_audio = (*env)->GetMethodID(env, listener_class, "onAudioFrame", "(II[S)V");
    jmethodID on_text_input_request =
        (*env)->GetMethodID(env, listener_class, "onTextInputRequest", "(ILjava/lang/String;)V");
    jmethodID on_mic_enable =
        (*env)->GetMethodID(env, listener_class, "onMicEnable", "(ZI)V");
    jmethodID on_disconnected =
        (*env)->GetMethodID(env, listener_class, "onDisconnected", "(Ljava/lang/String;)V");

    byte_buf buf = {0};
    int fd = -1;
    if (!connect_and_ws_upgrade(s->host, s->port, &s->stop, &fd, &buf)) {
        call_on_disconnected(env, s, on_disconnected, "Verbindung fehlgeschlagen");
        byte_buf_free(&buf);
        (*s->jvm)->DetachCurrentThread(s->jvm);
        return NULL;
    }
    s->sockfd = fd;

    app_handshake_result hs = perform_app_handshake(s, &buf);
    if (!hs.ok) {
        call_on_disconnected(env, s, on_disconnected, hs.reason);
        byte_buf_free(&buf);
        if (s->sockfd >= 0) {
            close(s->sockfd);
        }
        (*s->jvm)->DetachCurrentThread(s->jvm);
        return NULL;
    }

    atomic_store(&s->extended_input, hs.extended_input);
    (*env)->CallVoidMethod(env, s->listener, on_connected, (jboolean)hs.touch_input,
                            (jboolean)hs.extended_input);
    run_session_loop(env, s, on_video, on_audio, on_text_input_request, on_mic_enable, &buf);
    byte_buf_free(&buf);

    call_on_disconnected(env, s, on_disconnected, "Verbindung getrennt");

    close(s->sockfd);
    (*s->jvm)->DetachCurrentThread(s->jvm);
    return NULL;
}

JNIEXPORT jlong JNICALL Java_com_finlink_android_GbaStreamClient_nativeConnect(JNIEnv *env,
                                                                                jobject thiz,
                                                                                jstring jhost,
                                                                                jint jport,
                                                                                jobject listener) {
    (void)thiz;

    finlink_session *s = calloc(1, sizeof(finlink_session));
    if (!s) {
        return 0;
    }

    const char *host_chars = (*env)->GetStringUTFChars(env, jhost, NULL);
    strncpy(s->host, host_chars, sizeof(s->host) - 1);
    (*env)->ReleaseStringUTFChars(env, jhost, host_chars);

    s->port = (int)jport;
    s->sockfd = -1;
    (*env)->GetJavaVM(env, &s->jvm);
    s->listener = (*env)->NewGlobalRef(env, listener);
    atomic_init(&s->stop, false);
    atomic_init(&s->pending_keymask, 0);
    atomic_init(&s->input_dirty, false);
    atomic_init(&s->pending_touch_x, 0);
    atomic_init(&s->pending_touch_y, 0);
    atomic_init(&s->pending_touch_pressed, false);
    atomic_init(&s->touch_dirty, false);
    atomic_init(&s->extended_input, false);
    atomic_init(&s->pending_buttons, 0);
    atomic_init(&s->pending_left_x, 0);
    atomic_init(&s->pending_left_y, 0);
    atomic_init(&s->pending_right_x, 0);
    atomic_init(&s->pending_right_y, 0);
    pthread_mutex_init(&s->pending_text_response_mutex, NULL);
    atomic_init(&s->text_response_dirty, false);
    pthread_mutex_init(&s->pending_mic_audio_mutex, NULL);
    atomic_init(&s->mic_enabled, false);

    if (pthread_create(&s->thread, NULL, client_thread_main, s) != 0) {
        LOGE("pthread_create failed");
        (*env)->DeleteGlobalRef(env, s->listener);
        free(s);
        return 0;
    }

    return (jlong)(intptr_t)s;
}

JNIEXPORT void JNICALL Java_com_finlink_android_GbaStreamClient_nativeSendInput(JNIEnv *env,
                                                                                 jobject thiz,
                                                                                 jlong handle,
                                                                                 jint keymask) {
    (void)env;
    (void)thiz;
    finlink_session *s = (finlink_session *)(intptr_t)handle;
    if (!s) {
        return;
    }
    atomic_store(&s->pending_keymask, (int)keymask);
    atomic_store(&s->input_dirty, true);
}

JNIEXPORT void JNICALL Java_com_finlink_android_GbaStreamClient_nativeSendTouch(JNIEnv *env,
                                                                                  jobject thiz,
                                                                                  jlong handle,
                                                                                  jboolean pressed,
                                                                                  jint x, jint y) {
    (void)env;
    (void)thiz;
    finlink_session *s = (finlink_session *)(intptr_t)handle;
    if (!s) {
        return;
    }
    atomic_store(&s->pending_touch_pressed, (bool)pressed);
    atomic_store(&s->pending_touch_x, (int)x);
    atomic_store(&s->pending_touch_y, (int)y);
    atomic_store(&s->touch_dirty, true);
}

// Extended counterpart to nativeSendTouch -- only meaningful on an
// extended_input session (Listener.onConnected(isTouch = true,
// hasButtons = true)); harmless no-op on any other session since
// maybe_send_touch() only ever reads pending_buttons/pending_left_*/
// pending_right_* when s->extended_input is set in the first place.
// left_x/y is the circle pad or, on a two-stick console, the left stick;
// right_x/y is always 0 from a caller with only one stick to report (see
// finlink_extended_input's own comment, protocol.h).
JNIEXPORT void JNICALL Java_com_finlink_android_GbaStreamClient_nativeSendExtendedInput(
    JNIEnv *env, jobject thiz, jlong handle, jboolean touch_pressed, jint touch_x, jint touch_y,
    jint buttons, jint left_x, jint left_y, jint right_x, jint right_y) {
    (void)env;
    (void)thiz;
    finlink_session *s = (finlink_session *)(intptr_t)handle;
    if (!s) {
        return;
    }
    atomic_store(&s->pending_touch_pressed, (bool)touch_pressed);
    atomic_store(&s->pending_touch_x, (int)touch_x);
    atomic_store(&s->pending_touch_y, (int)touch_y);
    atomic_store(&s->pending_buttons, (int)buttons);
    atomic_store(&s->pending_left_x, (int)left_x);
    atomic_store(&s->pending_left_y, (int)left_y);
    atomic_store(&s->pending_right_x, (int)right_x);
    atomic_store(&s->pending_right_y, (int)right_y);
    atomic_store(&s->touch_dirty, true);
}

// Only meaningful right after Listener.onTextInputRequest() fires --
// confirmed=false (the user cancelled) sends an empty text regardless of
// jtext's content, matching finlink_text_input_response's own convention
// that text is meaningless when not confirmed.
JNIEXPORT void JNICALL Java_com_finlink_android_GbaStreamClient_nativeSendTextInputResponse(
    JNIEnv *env, jobject thiz, jlong handle, jboolean confirmed, jstring jtext) {
    (void)thiz;
    finlink_session *s = (finlink_session *)(intptr_t)handle;
    if (!s) {
        return;
    }

    const char *text_chars = confirmed ? (*env)->GetStringUTFChars(env, jtext, NULL) : NULL;
    size_t text_len = text_chars ? strlen(text_chars) : 0;

    char *text_copy = NULL;
    if (text_len > 0) {
        text_copy = malloc(text_len);
        if (text_copy) {
            memcpy(text_copy, text_chars, text_len);
        } else {
            text_len = 0;
        }
    }
    if (text_chars) {
        (*env)->ReleaseStringUTFChars(env, jtext, text_chars);
    }

    pthread_mutex_lock(&s->pending_text_response_mutex);
    free(s->pending_text_response_text); // in case a previous response never got sent
    s->pending_text_response_text = text_copy;
    s->pending_text_response_len = text_len;
    s->pending_text_response_confirmed = (bool)confirmed;
    pthread_mutex_unlock(&s->pending_text_response_mutex);
    atomic_store(&s->text_response_dirty, true);
}

// Called repeatedly off Kotlin's AudioRecord capture loop (see
// GbaStreamClient.Listener.onMicEnable's own comment) with whatever chunk
// of mono s16 samples it just read -- appends to pending_mic_audio for
// maybe_send_mic_audio() to drain, rather than sending directly from this
// thread, so the network thread stays the sole owner of the socket fd
// (same reasoning as every other nativeSend* function here). Caps the
// backlog at ~2s of audio at typical mic rates so a stalled network thread
// can't make this grow unbounded; drops the oldest data by resetting
// rather than blocking the capture thread, since a brief gap matters far
// less to the receiving game than an ever-growing queue would.
JNIEXPORT void JNICALL Java_com_finlink_android_GbaStreamClient_nativeSendMicAudio(
    JNIEnv *env, jobject thiz, jlong handle, jint sample_rate, jshortArray samples) {
    (void)thiz;
    finlink_session *s = (finlink_session *)(intptr_t)handle;
    if (!s) {
        return;
    }

    jsize sample_count = (*env)->GetArrayLength(env, samples);
    if (sample_count <= 0) {
        return;
    }
    jshort *elems = (*env)->GetShortArrayElements(env, samples, NULL);
    if (!elems) {
        return;
    }

    const size_t new_bytes = (size_t)sample_count * sizeof(int16_t);

    pthread_mutex_lock(&s->pending_mic_audio_mutex);
    // pending_mic_sample_rate is a single scalar tagging the *whole*
    // buffer, but that buffer can span multiple calls to this function
    // before maybe_send_mic_audio() drains it -- if the capture rate
    // changed since the last call (only possible today via
    // stopMicCapture()+startMicCapture() at a new rate, a narrow but real
    // window), mixing the new chunk into bytes still tagged with the old
    // rate would send a single frame whose header rate doesn't match part
    // of its own payload. Drop the stale backlog instead of mislabeling it.
    if (s->pending_mic_audio_len > 0 && s->pending_mic_sample_rate != (uint32_t)sample_rate) {
        s->pending_mic_audio_len = 0;
    }
    const size_t kMaxPendingBytes = 48000 * sizeof(int16_t) * 2; // ~2s at 48kHz mono
    if (s->pending_mic_audio_len + new_bytes > kMaxPendingBytes) {
        free(s->pending_mic_audio);
        s->pending_mic_audio = NULL;
        s->pending_mic_audio_len = 0;
        s->pending_mic_audio_cap = 0;
    }
    if (s->pending_mic_audio_len + new_bytes > s->pending_mic_audio_cap) {
        size_t new_cap = s->pending_mic_audio_cap == 0 ? 4096 : s->pending_mic_audio_cap * 2;
        while (new_cap < s->pending_mic_audio_len + new_bytes) {
            new_cap *= 2;
        }
        uint8_t *grown = realloc(s->pending_mic_audio, new_cap);
        if (grown) {
            s->pending_mic_audio = grown;
            s->pending_mic_audio_cap = new_cap;
        }
    }
    if (s->pending_mic_audio_len + new_bytes <= s->pending_mic_audio_cap) {
        for (jsize i = 0; i < sample_count; i++) {
            finlink_write_u16le(s->pending_mic_audio + s->pending_mic_audio_len + (size_t)i * 2,
                                 (uint16_t)elems[i]);
        }
        s->pending_mic_audio_len += new_bytes;
        s->pending_mic_sample_rate = (uint32_t)sample_rate;
    }
    pthread_mutex_unlock(&s->pending_mic_audio_mutex);

    (*env)->ReleaseShortArrayElements(env, samples, elems, JNI_ABORT);
}

JNIEXPORT void JNICALL Java_com_finlink_android_GbaStreamClient_nativeDisconnect(JNIEnv *env,
                                                                                  jobject thiz,
                                                                                  jlong handle) {
    (void)thiz;
    finlink_session *s = (finlink_session *)(intptr_t)handle;
    if (!s) {
        return;
    }
    atomic_store(&s->stop, true);
    pthread_join(s->thread, NULL);
    (*env)->DeleteGlobalRef(env, s->listener);
    pthread_mutex_destroy(&s->pending_text_response_mutex);
    free(s->pending_text_response_text);
    pthread_mutex_destroy(&s->pending_mic_audio_mutex);
    free(s->pending_mic_audio);
    free(s);
}
