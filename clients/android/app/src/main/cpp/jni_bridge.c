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
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "finlink/endian.h"
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

// Connects the session's socket and performs the WS handshake. On success,
// `leftover` holds any bytes received past the handshake response header --
// those are already WebSocket frame data and must feed straight into the
// session loop's receive buffer, not be discarded.
static bool do_connect_and_handshake(finlink_session *s, byte_buf *leftover) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", s->port);

    struct addrinfo *result = NULL;
    if (getaddrinfo(s->host, port_str, &hints, &result) != 0 || !result) {
        LOGE("getaddrinfo failed for %s:%d", s->host, s->port);
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
        LOGE("connect failed for %s:%d", s->host, s->port);
        return false;
    }
    s->sockfd = fd;

    uint8_t random_bytes[16];
    arc4random_buf(random_bytes, sizeof(random_bytes));
    char key[FINLINK_WS_KEY_BUF_LEN];
    finlink_ws_generate_key(random_bytes, key);

    char host_header[160];
    snprintf(host_header, sizeof(host_header), "%s:%d", s->host, s->port);

    char request[512];
    size_t request_len =
        finlink_ws_build_handshake_request(host_header, "/", key, request, sizeof(request));
    if (request_len == 0 || !send_all(fd, (const uint8_t *)request, request_len, &s->stop)) {
        LOGE("failed to send handshake request");
        return false;
    }

    byte_buf recv_buf = {0};
    uint8_t chunk[1024];
    finlink_ws_handshake_status status = FINLINK_WS_HANDSHAKE_INCOMPLETE;
    size_t header_len = 0;

    while (status == FINLINK_WS_HANDSHAKE_INCOMPLETE) {
        if (atomic_load(&s->stop)) {
            byte_buf_free(&recv_buf);
            return false;
        }
        ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            LOGE("connection closed during handshake");
            byte_buf_free(&recv_buf);
            return false;
        }
        byte_buf_append(&recv_buf, chunk, (size_t)n);
        if (recv_buf.len > 16384) { // guard against a runaway/malformed response
            LOGE("handshake response too large");
            byte_buf_free(&recv_buf);
            return false;
        }
        status = finlink_ws_parse_handshake_response(recv_buf.data, recv_buf.len, key, &header_len);
        if (status == FINLINK_WS_HANDSHAKE_ERR) {
            LOGE("handshake rejected (bad status or Sec-WebSocket-Accept mismatch)");
            byte_buf_free(&recv_buf);
            return false;
        }
    }

    byte_buf_append(leftover, recv_buf.data + header_len, recv_buf.len - header_len);
    byte_buf_free(&recv_buf);
    return true;
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

static void run_session_loop(JNIEnv *env, finlink_session *s, jmethodID on_video,
                              jmethodID on_audio, byte_buf *buf) {
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
                }
            }

            byte_buf_consume(buf, frame.frame_size);
        }
        if (should_stop) {
            break;
        }

        maybe_send_input(s);
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
    jmethodID on_connected = (*env)->GetMethodID(env, listener_class, "onConnected", "()V");
    jmethodID on_video = (*env)->GetMethodID(env, listener_class, "onVideoFrame", "(II[B)V");
    jmethodID on_audio = (*env)->GetMethodID(env, listener_class, "onAudioFrame", "(II[S)V");
    jmethodID on_disconnected =
        (*env)->GetMethodID(env, listener_class, "onDisconnected", "(Ljava/lang/String;)V");

    byte_buf buf = {0};
    if (!do_connect_and_handshake(s, &buf)) {
        call_on_disconnected(env, s, on_disconnected, "Verbindung/Handshake fehlgeschlagen");
        byte_buf_free(&buf);
        if (s->sockfd >= 0) {
            close(s->sockfd);
        }
        (*s->jvm)->DetachCurrentThread(s->jvm);
        return NULL;
    }

    (*env)->CallVoidMethod(env, s->listener, on_connected);
    run_session_loop(env, s, on_video, on_audio, &buf);
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
    free(s);
}
