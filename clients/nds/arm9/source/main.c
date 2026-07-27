/* finlink NDS client -- feasibility/bandwidth test, not a full player.
 *
 * Scope is deliberately minimal (see ../README.md and
 * ../../docs/nds-feasibility.md): the NDS's 802.11b WiFi hardware is
 * capped at 1-2 Mbit/s, well under what the wire protocol's audio stream
 * alone needs at its default sample rate, so before building a real UI
 * (menu, settings, GBA button overlay like the other three clients) this
 * just connects to a real finlink server, decodes whatever it receives
 * with the same core/ used everywhere else, and reports the actually
 * observed throughput/frame rate on real hardware -- the theoretical
 * numbers in nds-feasibility.md were never verified against a real
 * console. No audio playback (received but only counted, not queued to
 * any sound channel) -- unlike input, this is genuinely orthogonal to
 * the bandwidth question this build exists to answer, since the client
 * has no way to avoid receiving audio bytes over the wire either way
 * (see docs/protocol.md: no server-side mechanism to opt out). GBA
 * button input *is* now sent (see buildGbaKeyMask()/sendGbaInput()) --
 * negligible outbound bandwidth, and worth having for actually trying
 * the stream rather than just watching it.
 *
 * ARM9 side only: WiFi (Wifi_InitDefault() + stock BSD sockets, same API
 * as devkitPro's own examples/nds/dswifi/httpget) and all the
 * protocol/video/console logic below. The ARM7 side (../arm7/) is an
 * unmodified copy of devkitPro's templates/combined "default ARM7 core",
 * whose wlmgrStartServer() is what this file's dswifi9 calls actually
 * talk to -- see ../README.md for why this needs a real second ELF
 * rather than ds_rules' built-in default-ARM7 shortcut. */
#include <nds.h>
#include <dswifi9.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "subnet_discovery.h"
#include "finlink/endian.h"
#include "finlink/handshake.h"
#include "finlink/inflate.h"
#include "finlink/protocol.h"
#include "finlink/websocket.h"

static const int kSlotPorts[4] = { 6801, 6802, 6803, 6804 };

/* GBA screen size, per docs/protocol.md -- the video header does carry
 * width/height rather than hardcoding it, but this client only expects
 * the one resolution the server actually emulates and rejects anything
 * else (see runSession()), so buffers can be static/fixed-size instead
 * of malloc'd -- simpler and avoids heap fragmentation on a 4MB console. */
#define EXIT_HOLD_TICKS_REQUIRED 36 /* ~0.6s at 60Hz, matches clients/switch's kExitHoldSeconds */

#define GBA_W 240
#define GBA_H 160
#define FRAMEBUF_CAP (GBA_W * GBA_H * 2)
/* finlink_video_max_inflated_size(240, 160): width*height*2 + 4 +
 * tiles_per_row(30)*tiles_per_col(20)*2 + 512. Recomputed here as a
 * compile-time constant (with margin) rather than calling the function
 * at startup, so it can size a static array; see core/src/protocol.c. */
#define INFLATE_BUF_CAP (FRAMEBUF_CAP + 4 + 30 * 20 * 2 + 1024)

/* Flat receive buffer for not-yet-parsed WebSocket bytes. Must hold one
 * full WS frame even in the worst case (an incompressible video frame,
 * close to FRAMEBUF_CAP) -- but frames are parsed strictly front-first,
 * so on a slow link, audio frames that fully arrive *behind* a still-
 * incomplete video frame just pile up in this buffer too, unparsed,
 * until the video frame's tail finally shows up. A mere 4096-byte margin
 * (the original size here) turned out too tight on real hardware: a
 * hard-to-compress video frame plus even one or two trailing audio
 * frames overflowed it after just one successful frame ("Empfangspuffer
 * voll" in runSession()). This doesn't fix a genuine sustained bandwidth
 * mismatch (docs/nds-feasibility.md's core question) -- it just stops a
 * merely-tight margin from cutting the test short before that question
 * can even be observed. */
#define RECV_BUF_CAP (FRAMEBUF_CAP + 131072)
static uint8_t g_recvBuf[RECV_BUF_CAP];
static size_t g_recvLen = 0;

static uint8_t g_inflateBuf[INFLATE_BUF_CAP];
static uint8_t g_framebuf[FRAMEBUF_CAP];

/* User's screen preference for single-screen stream types (GC_GBA_LINK
 * today) -- toggled from slotSelectMenu(), applied in runSession(). Not
 * persisted (no save file exists in this client, see its own top comment):
 * resets to the default (top) every time the app is launched. */
static bool g_prefBottomScreen = false;

static void recvBufConsume(size_t n) {
    memmove(g_recvBuf, g_recvBuf + n, g_recvLen - n);
    g_recvLen -= n;
}

static void weakRandomBytes(uint8_t *out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        out[i] = (uint8_t)(rand() & 0xFF);
    }
}

/* Draws framebuffer_rgb565 (GBA_W x GBA_H, row-major u16le RGB565)
 * centered into VRAM_A, which main() put in MODE_FB0 (direct pixel
 * display, see nds/arm9/video.h) -- converts each pixel from the wire's
 * 5-6-5 RGB to the NDS framebuffer's 5-5-5 RGB + the mandatory bit-15
 * "opaque" flag (an FB-mode pixel with bit 15 clear is treated as
 * transparent/shows nothing). */
static void blitFrame(const uint8_t *rgb565) {
    const int offX = (256 - GBA_W) / 2;
    const int offY = (192 - GBA_H) / 2;
    for (int y = 0; y < GBA_H; y++) {
        const uint8_t *srcRow = rgb565 + (size_t)y * GBA_W * 2;
        uint16_t *dstRow = VRAM_A + (size_t)(y + offY) * 256 + offX;
        for (int x = 0; x < GBA_W; x++) {
            uint16_t px = finlink_read_u16le(srcRow + (size_t)x * 2);
            uint8_t r = (px >> 11) & 0x1F;
            uint8_t g = (px >> 5) & 0x3F;
            uint8_t b = px & 0x1F;
            dstRow[x] = RGB15(r, g >> 1, b) | BIT(15);
        }
    }
}

static bool sendAll(int fd, const uint8_t *data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        ssize_t n = send(fd, data + sent, size - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += (size_t)n;
    }
    return true;
}

/* NDS's D-pad/A/B/L/R/Start/Select already line up 1:1 with the GBA's own
 * button layout (same fixed mapping as clients/3ds/source/gba_buttons.hpp
 * uses for 3DS, which has the same physical layout) -- X/Y have no GBA
 * equivalent and are deliberately not mapped here, see runSession()'s
 * exit gesture below, which relies on exactly that gap. */
static uint16_t buildGbaKeyMask(int keys) {
    uint16_t mask = 0;
    if (keys & KEY_A) mask |= FINLINK_KEY_A;
    if (keys & KEY_B) mask |= FINLINK_KEY_B;
    if (keys & KEY_SELECT) mask |= FINLINK_KEY_SELECT;
    if (keys & KEY_START) mask |= FINLINK_KEY_START;
    if (keys & KEY_RIGHT) mask |= FINLINK_KEY_RIGHT;
    if (keys & KEY_LEFT) mask |= FINLINK_KEY_LEFT;
    if (keys & KEY_UP) mask |= FINLINK_KEY_UP;
    if (keys & KEY_DOWN) mask |= FINLINK_KEY_DOWN;
    if (keys & KEY_R) mask |= FINLINK_KEY_R;
    if (keys & KEY_L) mask |= FINLINK_KEY_L;
    return mask;
}

/* Sends one input frame for the current held-button state. Best-effort:
 * a dropped input frame isn't fatal (the next tick sends fresh state
 * regardless, there's no held queue), so failures here are silently
 * ignored rather than treated as a disconnect -- a real socket problem
 * will show up via recv() in the same tick's main loop anyway. */
static void sendGbaInput(int fd, uint16_t keyMask) {
    uint8_t payload[FINLINK_INPUT_FRAME_SIZE];
    finlink_build_input_frame(keyMask, payload);

    uint8_t maskKey[4];
    weakRandomBytes(maskKey, sizeof(maskKey));

    /* +14 = finlink_ws_build_frame_max_size()'s fixed overhead (10-byte
     * max header incl. length field + 4-byte mask key). */
    uint8_t frameBuf[FINLINK_INPUT_FRAME_SIZE + 14];
    size_t frame_len = finlink_ws_build_frame(FINLINK_WS_OPCODE_BINARY, payload, sizeof(payload), maskKey, frameBuf,
                                               sizeof(frameBuf));
    if (frame_len > 0) {
        sendAll(fd, frameBuf, frame_len);
    }
}

/* Blocking connect + RFC6455 handshake against host:port. On success,
 * *out_fd is the connected socket and any bytes received past the
 * handshake response header have already been copied into g_recvBuf
 * (they're live WS frame data, not to be discarded) -- same division as
 * clients/3ds/source/session.cpp's do_connect_and_handshake(). */
static bool connectAndHandshake(const char *host, int port, int *out_fd) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = inet_addr(host);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        closesocket(fd);
        return false;
    }

    uint8_t random_bytes[16];
    weakRandomBytes(random_bytes, sizeof(random_bytes));
    char key[FINLINK_WS_KEY_BUF_LEN];
    finlink_ws_generate_key(random_bytes, key);

    char host_header[64];
    snprintf(host_header, sizeof(host_header), "%s:%d", host, port);

    char request[512];
    size_t request_len = finlink_ws_build_handshake_request(host_header, "/", key, request, sizeof(request));
    if (request_len == 0 || !sendAll(fd, (const uint8_t *)request, request_len)) {
        closesocket(fd);
        return false;
    }

    uint8_t header_buf[2048];
    size_t header_len_total = 0;
    finlink_ws_handshake_status status = FINLINK_WS_HANDSHAKE_INCOMPLETE;
    size_t header_len = 0;

    while (status == FINLINK_WS_HANDSHAKE_INCOMPLETE) {
        if (header_len_total >= sizeof(header_buf)) {
            closesocket(fd);
            return false;
        }
        ssize_t n = recv(fd, header_buf + header_len_total, sizeof(header_buf) - header_len_total, 0);
        if (n <= 0) {
            closesocket(fd);
            return false;
        }
        header_len_total += (size_t)n;
        status = finlink_ws_parse_handshake_response(header_buf, header_len_total, key, &header_len);
        if (status == FINLINK_WS_HANDSHAKE_ERR) {
            closesocket(fd);
            return false;
        }
    }

    size_t leftover = header_len_total - header_len;
    if (leftover > RECV_BUF_CAP) {
        closesocket(fd);
        return false;
    }
    memcpy(g_recvBuf, header_buf + header_len, leftover);
    g_recvLen = leftover;

    *out_fd = fd;
    return true;
}

/* Blocking (no explicit timeout, same as connectAndHandshake() above --
 * this client has no watchdog for either phase) read from `fd` into the
 * global g_recvBuf/g_recvLen until one full WebSocket frame is available.
 * Frame data already sitting in g_recvBuf (e.g. left over from the WS
 * upgrade) is tried first before any recv().
 *
 * Deliberately does NOT consume the frame's bytes from g_recvBuf on
 * success -- out_frame->payload points *into* g_recvBuf, and
 * recvBufConsume() shifts g_recvBuf's contents down in place (memmove),
 * which would invalidate that pointer out from under the caller before it
 * ever reads it. The caller must finish reading out_frame->payload first,
 * then call recvBufConsume(out_frame->frame_size) itself once it's safe to
 * discard -- same bug (found from a live report on the Android client,
 * clients/android/.../jni_bridge.c's history has the postmortem) this
 * avoids by construction rather than fixing after the fact. */
static bool receiveOneWsFrame(int fd, finlink_ws_frame *outFrame) {
    for (;;) {
        finlink_ws_frame_status fs = finlink_ws_parse_frame(g_recvBuf, g_recvLen, outFrame);
        if (fs == FINLINK_WS_FRAME_OK) {
            return true;
        }
        if (fs == FINLINK_WS_FRAME_ERR) {
            return false;
        }
        if (g_recvLen >= RECV_BUF_CAP) {
            return false; /* frame won't ever fit -- same guard as runSession()'s overflow check */
        }
        ssize_t n = recv(fd, g_recvBuf + g_recvLen, RECV_BUF_CAP - g_recvLen, 0);
        if (n <= 0) {
            return false;
        }
        g_recvLen += (size_t)n;
    }
}

/* GC_GBA_LINK player ports are always this + the GC device number
 * (docs/protocol.md; matches kSlotPorts[] above and
 * GBA_STREAM_PLAYER_BASE_PORT in the dolphin-gba-stream fork). */
#define PLAYER_BASE_PORT 6801

/* App-level handshake (finlink/handshake.h, docs/protocol.md
 * "Verbindungsaufbau: Handshake"), run once *fd is already WS-upgraded
 * (g_recvBuf may already hold the server's first message -- see
 * connectAndHandshake()). On a session_ready with a redirect (only
 * possible for multi-slot stream types via the lobby port -- not
 * reachable through this client's own "connect straight to a player
 * port" slot menu, but handled anyway per docs/protocol.md), closes *fd
 * and reconnects to the redirect target, repeating once (matching the
 * protocol's own one-hop design). outReason must be at least 96 bytes --
 * always NUL-terminated on return, empty string on success. */
static bool performAppHandshake(int *fd, char *host, size_t hostCap, int *port, char *outReason,
                                 size_t outReasonCap, char outStreamType[FINLINK_STREAM_TYPE_LEN]) {
    outReason[0] = '\0';
    outStreamType[0] = '\0';

    for (int hop = 0; hop < 2; hop++) {
        finlink_ws_frame frame;
        if (!receiveOneWsFrame(*fd, &frame)) {
            snprintf(outReason, outReasonCap, "Server hat keinen Handshake gestartet");
            return false;
        }
        if (frame.opcode != FINLINK_WS_OPCODE_TEXT ||
            finlink_peek_handshake_message(frame.payload, frame.payload_size) != FINLINK_HS_MSG_HELLO) {
            snprintf(outReason, outReasonCap, "Unerwartete erste Nachricht vom Server");
            return false;
        }

        finlink_hello hello;
        finlink_handshake_result helloParsed = finlink_parse_hello(frame.payload, frame.payload_size, &hello);
        recvBufConsume(frame.frame_size); /* done reading frame.payload either way */
        if (helloParsed != FINLINK_HANDSHAKE_OK) {
            snprintf(outReason, outReasonCap, "hello konnte nicht gelesen werden");
            return false;
        }
        if (hello.protocol_version != FINLINK_PROTOCOL_VERSION) {
            snprintf(outReason, outReasonCap, "Server: Protokollversion %d, Client: %d",
                     hello.protocol_version, FINLINK_PROTOCOL_VERSION);
            return false;
        }
        /* Last hop wins on a redirect -- the redirect target's own hello is
         * the authoritative one for the connection that actually carries
         * stream data (see below). */
        strncpy(outStreamType, hello.stream_type, FINLINK_STREAM_TYPE_LEN - 1);
        outStreamType[FINLINK_STREAM_TYPE_LEN - 1] = '\0';

        /* This client always dials a specific already-chosen player port
         * (see kSlotPorts[]/slotSelectMenu()), never the lobby port -- so
         * the slot being asked about is simply "the one this connection
         * is on". */
        finlink_hello_ack_request ackReq;
        memset(&ackReq, 0, sizeof(ackReq));
        ackReq.requested_slot = *port - PLAYER_BASE_PORT;
        ackReq.max_width = hello.video.width > 0 ? hello.video.width : GBA_W;
        ackReq.max_height = hello.video.height > 0 ? hello.video.height : GBA_H;
        ackReq.max_fps = hello.video.fps > 0 ? hello.video.fps : 60.0;
        /* No audio playback here regardless of what the server offers (see
         * this file's top comment) -- still requesting it anyway would
         * make the server send audio frames this client has to receive
         * and discard either way (docs/protocol.md: no opt-out
         * mechanism), so wants_audio=0 at least keeps this client's own
         * *request* honest about what it actually uses. */
        ackReq.wants_audio = 0;

        char ackJson[256];
        size_t ackLen = finlink_build_hello_ack(&ackReq, ackJson, sizeof(ackJson));
        if (ackLen == 0) {
            snprintf(outReason, outReasonCap, "hello_ack zu gross fuer den Puffer");
            return false;
        }

        uint8_t maskKey[4];
        weakRandomBytes(maskKey, sizeof(maskKey));
        uint8_t frameBuf[256 + 14];
        size_t frameLen = finlink_ws_build_frame(FINLINK_WS_OPCODE_TEXT, (const uint8_t *)ackJson, ackLen, maskKey,
                                                  frameBuf, sizeof(frameBuf));
        if (frameLen == 0 || !sendAll(*fd, frameBuf, frameLen)) {
            snprintf(outReason, outReasonCap, "hello_ack konnte nicht gesendet werden");
            return false;
        }

        finlink_ws_frame reply;
        if (!receiveOneWsFrame(*fd, &reply)) {
            snprintf(outReason, outReasonCap, "keine Antwort auf hello_ack");
            return false;
        }
        if (reply.opcode != FINLINK_WS_OPCODE_TEXT) {
            snprintf(outReason, outReasonCap, "unerwartete Antwort auf hello_ack");
            return false;
        }

        finlink_handshake_message_type replyType = finlink_peek_handshake_message(reply.payload, reply.payload_size);
        if (replyType == FINLINK_HS_MSG_HANDSHAKE_ERROR) {
            finlink_handshake_error err;
            if (finlink_parse_handshake_error(reply.payload, reply.payload_size, &err) == FINLINK_HANDSHAKE_OK) {
                snprintf(outReason, outReasonCap, "%s", err.detail);
            } else {
                snprintf(outReason, outReasonCap, "Handshake vom Server abgelehnt");
            }
            recvBufConsume(reply.frame_size);
            return false;
        }
        if (replyType != FINLINK_HS_MSG_SESSION_READY) {
            snprintf(outReason, outReasonCap, "unerwartete Antwort auf hello_ack");
            recvBufConsume(reply.frame_size);
            return false;
        }

        finlink_session_ready ready;
        finlink_handshake_result readyParsed = finlink_parse_session_ready(reply.payload, reply.payload_size, &ready);
        recvBufConsume(reply.frame_size); /* done reading reply.payload either way */
        if (readyParsed != FINLINK_HANDSHAKE_OK) {
            snprintf(outReason, outReasonCap, "session_ready konnte nicht gelesen werden");
            return false;
        }

        if (!ready.has_redirect) {
            return true;
        }

        /* Redirect: this connection carries no stream data, ever -- close
         * it, reconnect to the target, and let the loop try the same
         * exchange again there (hop 1, the only one this loop allows). */
        closesocket(*fd);
        *fd = -1;
        g_recvLen = 0;

        strncpy(host, ready.redirect_host, hostCap - 1);
        host[hostCap - 1] = '\0';
        *port = ready.redirect_port;

        if (!connectAndHandshake(host, *port, fd)) {
            snprintf(outReason, outReasonCap, "Verbindung zum weitergeleiteten Port fehlgeschlagen");
            return false;
        }
        /* loop: expect a fresh hello on the new connection */
    }

    snprintf(outReason, outReasonCap, "zu viele Weiterleitungen");
    return false;
}

typedef struct {
    unsigned videoFrames, videoBytes;
    unsigned audioFrames, audioBytes;
    unsigned decodeErrors;
} Stats;

/* Runs one connection's receive/decode loop until disconnected or the
 * user presses START. Paced by swiWaitForVBlank() (60Hz) rather than a
 * poll()/select() timeout -- neither is implemented by libnds' socket
 * layer (only MSG_PEEK among recv() flags, per sys/socket.h). The socket
 * is set non-blocking (FIONBIO) so a single per-tick recv() always
 * returns immediately -- with data if any has arrived, EWOULDBLOCK
 * otherwise -- rather than risking an indefinite block that would also
 * freeze the START-to-quit key check whenever the stream goes idle
 * (normal per docs/protocol.md's video dedup: no new frame is not an
 * error). One 4KB-chunked read per 16.7ms tick comfortably drains far
 * more than the ~2-4KB/tick this link can theoretically deliver at
 * 1-2 Mbit/s, so this doesn't cap actual throughput either. */
static void runSession(const char *hostIn, int portIn) {
    consoleClear();
    iprintf("Verbinde zu %s:%d ...\n", hostIn, portIn);

    /* Local mutable copies -- performAppHandshake() may rewrite both on a
     * redirect (see its own comment; not reachable via this client's own
     * slot menu today, but handled per docs/protocol.md regardless). */
    char host[64];
    strncpy(host, hostIn, sizeof(host) - 1);
    host[sizeof(host) - 1] = '\0';
    int port = portIn;

    int fd;
    if (!connectAndHandshake(host, port, &fd)) {
        iprintf("Verbindung fehlgeschlagen.\n");
        iprintf("\nTaste druecken fuer Menue.\n");
        while (true) {
            swiWaitForVBlank();
            scanKeys();
            if (keysDown()) {
                return;
            }
        }
    }

    /* App-level handshake (finlink/handshake.h): must succeed -- version
     * match, this slot requested and free -- before any Video/Audio/Input
     * binary frame is allowed on this connection. May reconnect fd/host/
     * port once, on a redirect. */
    char handshakeFailReason[96];
    char streamType[FINLINK_STREAM_TYPE_LEN];
    if (!performAppHandshake(&fd, host, sizeof(host), &port, handshakeFailReason, sizeof(handshakeFailReason),
                              streamType)) {
        if (fd >= 0) {
            closesocket(fd);
        }
        iprintf("Handshake fehlgeschlagen:\n%s\n", handshakeFailReason);
        iprintf("\nTaste druecken fuer Menue.\n");
        while (true) {
            swiWaitForVBlank();
            scanKeys();
            if (keysDown()) {
                return;
            }
        }
    }

    int nonblocking = 1;
    ioctl(fd, FIONBIO, &nonblocking);

    /* Which physical screen shows the video (MAIN engine, VRAM_A) vs. the
     * text console/stats (SUB engine, see main()'s videoSetMode/
     * consoleDemoInit) -- lcdMainOnBottom() swaps the whole MAIN/SUB-to-
     * physical-panel assignment, so this is the entire "move video to the
     * other screen" operation, no drawing-code changes needed either way.
     * A stream_type that's itself a dual-screen source's secondary screen
     * (docs/protocol.md) always goes to this client's own bottom screen,
     * overriding g_prefBottomScreen -- see
     * finlink_stream_type_prefers_secondary_screen(). */
    if (g_prefBottomScreen || finlink_stream_type_prefers_secondary_screen(streamType)) {
        lcdMainOnBottom();
    } else {
        lcdMainOnTop();
    }

    consoleClear();
    iprintf("Verbunden. X+Y halten zum Beenden.\n\n");
    iprintf("Video: -- fps  -- KB/s\n");
    iprintf("Audio: -- fps  -- KB/s\n");
    iprintf("Frames total: 0   Fehler: 0\n");

    Stats window;
    memset(&window, 0, sizeof(window));
    unsigned totalVideoFrames = 0, totalDecodeErrors = 0;
    int tick = 0;
    /* Set on every non-user-initiated exit from the loop below so the
     * "Getrennt" screen says *why* instead of just *that* -- added after
     * a real-hardware report of "one video frame, then disconnected"
     * that this client couldn't diagnose any further on its own. NULL
     * means the user quit deliberately (X+Y hold below), which isn't a
     * disconnect. */
    const char *disconnectReason = NULL;
    /* X+Y held together exits back to the menu -- not START/SELECT, since
     * those (like every other GBA button) are now real, sendable input
     * once a session is active; overloading one of them for "quit" would
     * mean it could never actually be sent to the game. X/Y have no GBA
     * equivalent (buildGbaKeyMask() never sets a bit for them) and NDS
     * has no spare shoulder/HOME button the other two clients could use
     * instead (clients/switch: ZL+ZR hold; clients/3ds: touchscreen
     * button) -- held rather than a single tap, same reasoning as
     * Switch's hold-to-exit, to avoid an accidental mid-game disconnect. */
    int exitHoldTicks = 0;

    for (;;) {
        swiWaitForVBlank();
        scanKeys();
        int heldKeys = keysHeld();
        if ((heldKeys & (KEY_X | KEY_Y)) == (KEY_X | KEY_Y)) {
            if (++exitHoldTicks >= EXIT_HOLD_TICKS_REQUIRED) {
                break;
            }
        } else {
            exitHoldTicks = 0;
        }
        sendGbaInput(fd, buildGbaKeyMask(heldKeys));

        /* Drain every byte the link has already delivered -- and parse/
         * consume whatever that completes -- before waiting for the next
         * vblank, instead of one fixed 4KB read per tick. A single
         * bounded read per tick left room for backlog to sit in dswifi's
         * own socket buffer an extra tick or more before we even looked
         * at it; this closes that gap (real bottleneck is still how fast
         * bytes cross the WiFi link, not how promptly we drain them once
         * they have -- see the disconnect diagnosis in this session). */
        for (;;) {
            uint8_t chunk[4096];
            ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
            if (n == 0) {
                disconnectReason = "Server hat Verbindung geschlossen";
                goto disconnected;
            }
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break; /* nothing more waiting right now */
                }
                static char errBuf[48];
                snprintf(errBuf, sizeof(errBuf), "Socket-Fehler (errno %d)", errno);
                disconnectReason = errBuf;
                goto disconnected;
            }
            if (g_recvLen + (size_t)n > RECV_BUF_CAP) {
                disconnectReason = "Empfangspuffer voll (Overflow)";
                goto disconnected;
            }
            memcpy(g_recvBuf + g_recvLen, chunk, (size_t)n);
            g_recvLen += (size_t)n;

            for (;;) {
                finlink_ws_frame frame;
                finlink_ws_frame_status fs = finlink_ws_parse_frame(g_recvBuf, g_recvLen, &frame);
                if (fs == FINLINK_WS_FRAME_INCOMPLETE) {
                    break;
                }
                if (fs == FINLINK_WS_FRAME_ERR) {
                    static char errBuf[48];
                    snprintf(errBuf, sizeof(errBuf), "WS-Frame-Fehler (%u B im Puffer)", (unsigned)g_recvLen);
                    disconnectReason = errBuf;
                    recvBufConsume(frame.frame_size <= g_recvLen ? frame.frame_size : g_recvLen);
                    goto disconnected;
                }
                if (frame.opcode == FINLINK_WS_OPCODE_CLOSE) {
                    disconnectReason = "Server-Close-Frame empfangen";
                    recvBufConsume(frame.frame_size <= g_recvLen ? frame.frame_size : g_recvLen);
                    goto disconnected;
                }

                finlink_msg_type type;
                if (finlink_peek_type(frame.payload, frame.payload_size, &type) == FINLINK_OK) {
                    if (type == FINLINK_MSG_VIDEO) {
                        finlink_video_header hdr;
                        if (finlink_parse_video_header(frame.payload, frame.payload_size, &hdr) == FINLINK_OK) {
                            if (hdr.width != GBA_W || hdr.height != GBA_H) {
                                window.decodeErrors++;
                            } else {
                                size_t inflated_size = 0;
                                if (finlink_inflate_raw(hdr.compressed_data, hdr.compressed_size, g_inflateBuf,
                                                         sizeof(g_inflateBuf), &inflated_size) == FINLINK_INFLATE_OK &&
                                    finlink_decode_video_frame(hdr.format, g_inflateBuf, inflated_size, hdr.width,
                                                                hdr.height, g_framebuf, sizeof(g_framebuf)) == FINLINK_OK) {
                                    window.videoFrames++;
                                    window.videoBytes += (unsigned)frame.payload_size;
                                    totalVideoFrames++;
                                    blitFrame(g_framebuf);
                                } else {
                                    window.decodeErrors++;
                                    totalDecodeErrors++;
                                }
                            }
                        }
                    } else if (type == FINLINK_MSG_AUDIO) {
                        /* Experiment (see conversation): skip parsing
                         * entirely to test whether video throughput
                         * improves without it. It doesn't change how many
                         * bytes must arrive over WiFi before this frame is
                         * even recognized as complete
                         * (finlink_ws_parse_frame() above already required
                         * the whole thing in g_recvBuf), so this is
                         * expected to make no difference -- audio's bytes
                         * still occupy the link and this buffer either
                         * way. Kept deliberately minimal (no stats at all)
                         * rather than restructured to discard-while-
                         * streaming, which would actually reduce buffer
                         * pressure but needs peeking at a frame header
                         * before it's fully arrived -- core/'s
                         * finlink_ws_parse_frame() doesn't expose that
                         * today. */
                    }
                }

                recvBufConsume(frame.frame_size);
            }
        }

        if (++tick >= 60) { /* ~1s at 60Hz */
            tick = 0;
            iprintf("\x1b[2;0HVideo: %2u fps  %3u KB/s   \n", window.videoFrames, window.videoBytes / 1024);
            iprintf("\x1b[3;0HAudio: %2u fps  %3u KB/s   \n", window.audioFrames, window.audioBytes / 1024);
            iprintf("\x1b[4;0HFrames total: %u   Fehler: %u   \n", totalVideoFrames, totalDecodeErrors);
            memset(&window, 0, sizeof(window));
        }
    }

disconnected:
    closesocket(fd);
    if (disconnectReason != NULL) {
        iprintf("\nGetrennt: %s\n", disconnectReason);
    } else {
        iprintf("\nGetrennt.\n");
    }
    iprintf("Taste druecken fuer Menue.\n");
    while (true) {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown()) {
            return;
        }
    }
}

/* On-screen IP entry -- same role as the other clients' swkbd-based host
 * prompt (clients/3ds/source/main.cpp's promptForHost(), clients/switch's
 * InputCell), just via libnds's stdin-integrated keyboard (see main()'s
 * keyboardDemoInit() call) instead of libctru/libnx's swkbd. None of the
 * other clients validate the format beyond non-empty either -- an
 * invalid address here just fails to connect the same way an unreachable
 * one does, so this doesn't add format checking either. */
/* outIp must be a 16-byte buffer (matching serverIp at both call sites in
 * main()) -- 15 chars is enough for the longest dotted-decimal IPv4
 * address plus a NUL, and the width limit below matches that exactly so
 * there's no separate truncating copy to get wrong. */
/* Echoes typed keys to the console -- without this, the keyboard itself
 * is visible but nothing shows what's actually been typed so far, per a
 * real-hardware report of typing the IP blind. Same approach as
 * devkitPro's own nds-examples/input/keyboard/keyboard_stdin example:
 * only printable keys are echoed (key > 0), control keys like backspace
 * (DVK_BACKSPACE) aren't specially handled -- the underlying stdin buffer
 * still processes backspace correctly, it just doesn't visually erase
 * the character on this simple tile console. */
static void onKeyboardKeyPressed(int key) {
    if (key > 0) {
        iprintf("%c", key);
    }
}

static void promptForIp(char outIp[16]) {
    consoleClear();
    iprintf("finlink NDS - Machbarkeitstest\n\n");
    iprintf("IP-Adresse des Servers eingeben:\n");
    iprintf("(z.B. 192.168.1.100)\n\n");
    outIp[0] = '\0';
    iscanf("%15s", outIp);
}

/* Returns 0-3 for the chosen slot, -1 to quit, -2 to re-run discovery,
 * -3 to enter the server IP manually. */
static int slotSelectMenu(const char *ownIp, const char *serverIp, bool autoDiscovered) {
    consoleClear();
    iprintf("finlink NDS - Machbarkeitstest\n\n");
    iprintf("Server: %s%s\n", serverIp, autoDiscovered ? " (gefunden)" : " (manuell)");
    iprintf("Eigene IP: %s\n\n", ownIp);
    iprintf("Slot waehlen:\n");
    iprintf(" A = Slot 1 (Port 6801)\n");
    iprintf(" B = Slot 2 (Port 6802)\n");
    iprintf(" X = Slot 3 (Port 6803)\n");
    iprintf(" Y = Slot 4 (Port 6804)\n\n");
    iprintf(" SELECT = Server erneut suchen\n");
    iprintf(" R = IP manuell eingeben\n");
    iprintf(" L = Bildschirm umschalten\n");
    iprintf(" START = Beenden\n\n");
    /* Only affects single-screen stream types (GC_GBA_LINK today) -- a
     * stream_type that's itself a dual-screen source's secondary screen
     * always goes to this client's bottom screen regardless, so this
     * setting is only actually consulted in that case. See runSession(). */
    iprintf("\x1b[16;0HBildschirm: %s   ", g_prefBottomScreen ? "unten" : "oben");

    for (;;) {
        swiWaitForVBlank();
        scanKeys();
        int keys = keysDown();
        if (keys & KEY_A) return 0;
        if (keys & KEY_B) return 1;
        if (keys & KEY_X) return 2;
        if (keys & KEY_Y) return 3;
        if (keys & KEY_SELECT) return -2;
        if (keys & KEY_R) return -3;
        if (keys & KEY_L) {
            g_prefBottomScreen = !g_prefBottomScreen;
            iprintf("\x1b[16;0HBildschirm: %s   ", g_prefBottomScreen ? "unten" : "oben");
        }
        if (keys & KEY_START) return -1;
    }
}

/* Runs the non-blocking subnet scan (discovery.h/.c) to completion or
 * until the user presses START to skip it, showing progress on-screen.
 * Returns true and fills *outIp (dotted-decimal) if a lobby was found;
 * false otherwise (caller prompts for the IP manually, see main()). */
static bool runDiscovery(uint32_t ownIpRaw, char *outIp, size_t outIpCap) {
    consoleClear();
    iprintf("finlink NDS - Machbarkeitstest\n\n");
    iprintf("Suche Server im lokalen Netz (Port 6800)...\n");
    iprintf("START = ueberspringen (IP manuell eingeben)\n\n");

    DiscoveryScan scan;
    discovery_start(&scan, ownIpRaw);
    if (scan.hostCount == 0) {
        return false;
    }

    for (;;) {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown() & KEY_START) {
            discovery_abort(&scan);
            return false;
        }
        bool finished = discovery_tick(&scan);
        iprintf("\x1b[3;0H%d/%d geprueft...   \n", scan.doneCount, scan.hostCount);
        if (finished) {
            break;
        }
    }

    if (!scan.found) {
        return false;
    }
    struct in_addr in;
    in.s_addr = scan.foundIp;
    strncpy(outIp, inet_ntoa(in), outIpCap - 1);
    outIp[outIpCap - 1] = '\0';
    return true;
}

int main(void) {
    videoSetMode(MODE_FB0);
    vramSetBankA(VRAM_A_LCD);
    memset(VRAM_A, 0, 256 * 192 * 2);

    consoleDemoInit();
    keyboardDemoInit()->OnKeyPressed = onKeyboardKeyPressed; /* see promptForIp() */
    iprintf("finlink NDS - Machbarkeitstest\n\n");
    iprintf("Verbinde WLAN (WFC)...\n");

    if (!Wifi_InitDefault(WFC_CONNECT)) {
        iprintf("WLAN-Verbindung fehlgeschlagen.\n");
        iprintf("(WFC-Zugangsdaten in den DS-Systemeinstellungen pruefen.)\n");
        while (true) {
            swiWaitForVBlank();
            scanKeys();
        }
    }

    struct in_addr ip;
    uint32_t ownIpRaw = Wifi_GetIP();
    ip.s_addr = ownIpRaw;
    char ownIp[16];
    strncpy(ownIp, inet_ntoa(ip), sizeof(ownIp) - 1);
    ownIp[sizeof(ownIp) - 1] = '\0';

    char serverIp[16];
    serverIp[0] = '\0';
    bool autoDiscovered = runDiscovery(ownIpRaw, serverIp, sizeof(serverIp));
    if (!autoDiscovered) {
        promptForIp(serverIp);
    }

    for (;;) {
        int slot = slotSelectMenu(ownIp, serverIp, autoDiscovered);
        if (slot == -2) {
            autoDiscovered = runDiscovery(ownIpRaw, serverIp, sizeof(serverIp));
            if (!autoDiscovered) {
                promptForIp(serverIp);
            }
            continue;
        }
        if (slot == -3) {
            promptForIp(serverIp);
            autoDiscovered = false;
            continue;
        }
        if (slot < 0) {
            break;
        }
        runSession(serverIp, kSlotPorts[slot]);
        /* runSession() may have swapped MAIN/SUB to the bottom screen (see
         * its own comment) -- every one of its exit paths returns here, so
         * resetting once, in this one place, is enough to keep every menu
         * screen back on its usual (top-screen-video) layout regardless of
         * how the session ended. */
        lcdMainOnTop();
    }

    return 0;
}
