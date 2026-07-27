#include "session.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

extern "C" {
#include "finlink/endian.h"
#include "finlink/inflate.h"
#include "finlink/protocol.h"
#include "finlink/websocket.h"
}

namespace {

// rand(), not arc4random_buf(): devkitARM's newlib routes arc4random_buf()
// through getentropy(), which has no 3DS syscall backend and fails to
// link (same issue as devkitA64, see clients/switch/source/session.cpp).
// The WS handshake key only needs to be unpredictable enough to defeat
// naive proxy caching (see finlink/websocket.h), not cryptographically
// strong, so libc rand() is fine here.
void weakRandomBytes(uint8_t *out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        out[i] = static_cast<uint8_t>(rand() & 0xFF);
    }
}

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

// On success, `leftover` holds any bytes received past the handshake
// response header -- already WebSocket frame data, must feed straight into
// the session loop's receive buffer, not be discarded.
bool do_connect_and_handshake(const std::string &host, int port, int *out_fd, std::atomic<bool> *stop_flag,
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
    weakRandomBytes(random_bytes, sizeof(random_bytes));
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

} // namespace

GbaSession::~GbaSession() {
    disconnect();
}

void GbaSession::connect(std::string host, int port, Listener l) {
    listener = std::move(l);
    stop.store(false);
    thread = std::thread(&GbaSession::threadMain, this, std::move(host), port);
}

void GbaSession::sendInput(uint16_t keyMask) {
    pendingKeymask.store(keyMask);
    inputDirty.store(true);
}

void GbaSession::disconnect() {
    stop.store(true);
    if (thread.joinable()) {
        thread.join();
    }
}

void GbaSession::threadMain(std::string host, int port) {
    RecvBuffer buf;
    if (!do_connect_and_handshake(host, port, &sockfd, &stop, &buf)) {
        if (sockfd >= 0) {
            close(sockfd);
            sockfd = -1;
        }
        if (listener.onDisconnected) {
            listener.onDisconnected("Verbindung/Handshake fehlgeschlagen");
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
            weakRandomBytes(mask_key, sizeof(mask_key));

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
    if (listener.onDisconnected) {
        listener.onDisconnected("Verbindung getrennt");
    }
}
