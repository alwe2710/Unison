/* finlink NDS client -- feasibility/bandwidth test, not a full player.
 *
 * Scope is deliberately minimal (see ../README.md and
 * ../../docs/nds-feasibility.md): the NDS's 802.11b WiFi hardware is
 * capped at 1-2 Mbit/s, so before building a real UI (menu, settings, GBA
 * button overlay like the other three clients) this just connects to a
 * real finlink server, decodes whatever it receives with the same core/
 * used everywhere else, and reports the actually observed throughput/
 * frame rate on real hardware -- the theoretical numbers in
 * nds-feasibility.md were never verified against a real console. Audio
 * playback (see audioStreamRequest()/g_audioRing) requests mono explicitly
 * (ackReq.max_channels = 1 in performAppHandshake()) specifically to keep
 * its share of that same tight budget down. GBA button input is also sent
 * (see buildGbaKeyMask()/sendGbaInput()) -- negligible outbound bandwidth,
 * and worth having for actually trying the stream rather than just
 * watching it.
 *
 * ARM9 side only: WiFi (Wifi_InitDefault() + stock BSD sockets, same API
 * as devkitPro's own examples/nds/dswifi/httpget), audio playback
 * (maxmod9.h's streaming API, examples/nds/audio/maxmod/streaming/ is the
 * reference this follows), and all the protocol/video/console logic
 * below. The ARM7 side (../arm7/) is an unmodified copy of devkitPro's
 * templates/combined "default ARM7 core", whose wlmgrStartServer() is
 * what this file's dswifi9 calls actually talk to, and whose
 * mmInstall() call (already present in that same unmodified file) is
 * what makes the maxmod9 calls below work with zero ARM7-side changes --
 * see ../README.md for why this needs a real second ELF rather than
 * ds_rules' built-in default-ARM7 shortcut. */
#include <nds.h>
#include <dswifi9.h>
#include <maxmod9.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "beacon_discovery.h"
#include "strings_generated.h"
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

/* false (default) = centered 1:1, every GBA pixel exactly one NDS pixel,
 * bordered on all four sides. true = uniformly upscaled to fill as much of
 * the 256x192 screen as possible *without* distorting the GBA's 3:2 aspect
 * ratio against the screen's 4:3 -- the screen's width is the tighter
 * constraint (256/240 < 192/160), so this fills the screen edge-to-edge
 * horizontally and leaves only a small letterbox top/bottom, rather than
 * stretching to fill both axes independently. See blitFrame(). */
static bool g_prefAspectScale = false;

/* false (default) = nearest-neighbor upscale in blitFrameScaled() (crisp/
 * pixelated), true = bilinear (smooth) -- same "per console" preference
 * the other four clients offer, collapsed to a single toggle here rather
 * than a per-stream-type map: this client only ever accepts GC_GBA_LINK's
 * fixed 240x160 GBA resolution (see runSession()'s hdr.width/height check),
 * so there's only ever one stream_type this setting could apply to in
 * practice. Only affects blitFrameScaled() -- the centered-1:1 default
 * (blitFrame() with g_prefAspectScale off) has no scaling to filter, every
 * destination pixel already maps to exactly one source pixel. */
static bool g_prefBilinear = false;

/* -1 = System (default, resolves via PersonalData->language -- see
 * applyLanguage() below), otherwise an explicit StrLang value picked from
 * languageMenu(). Same "not persisted" caveat as the toggles above:
 * resets to System (i.e. re-reads the console's own language) on every
 * launch. STR_* text updates immediately since strSetLanguage()
 * (strings_generated.h) just repoints the same STR_FOO globals every
 * existing call site already reads. */
static int g_prefLanguage = -1;

/* Sent verbatim as hello_ack.video_mode during the handshake (see
 * performAppHandshake()) -- one of the wire-format strings finlink's
 * docs/protocol.md defines ("tiles"/"legacy"/"h264"/"h265"), picked from
 * videoModeMenu(). Same "not persisted" caveat as g_prefLanguage above:
 * resets to "tiles" on every launch. */
static char g_prefVideoMode[16] = "tiles";

static void applyLanguage(void) {
    if (g_prefLanguage >= 0) {
        strSetLanguage((StrLang)g_prefLanguage);
        return;
    }
    /* PersonalData->language (libnds nds/system.h): 0=Japanese,
     * 1=English, 2=French, 3=German, 4=Italian, 5=Spanish, 6=Chinese,
     * 7=Unknown/Reserved -- anything this app has no translation for
     * (including Japanese/Chinese/Unknown) falls back to English, same
     * policy as every other client. */
    switch (PersonalData->language) {
    case 3: strSetLanguage(STR_LANG_DE); break;
    case 2: strSetLanguage(STR_LANG_FR); break;
    case 4: strSetLanguage(STR_LANG_IT); break;
    case 5: strSetLanguage(STR_LANG_ES); break;
    default: strSetLanguage(STR_LANG_EN); break;
    }
}

/* Endonym for the current g_prefLanguage value, for slotSelectMenu()'s
 * status line -- STR_LANGUAGE_SYSTEM for the -1 sentinel. */
static const char *languagePrefLabel(void) {
    switch (g_prefLanguage) {
    case STR_LANG_DE: return STR_LANGUAGE_GERMAN;
    case STR_LANG_EN: return STR_LANGUAGE_ENGLISH;
    case STR_LANG_FR: return STR_LANGUAGE_FRENCH;
    case STR_LANG_IT: return STR_LANGUAGE_ITALIAN;
    case STR_LANG_ES: return STR_LANGUAGE_SPANISH;
    default: return STR_LANGUAGE_SYSTEM;
    }
}

/* Label for the current g_prefVideoMode value, for slotSelectMenu()'s
 * status line -- mirrors languagePrefLabel() above. */
static const char *videoModePrefLabel(void) {
    if (strcmp(g_prefVideoMode, "h264") == 0) return STR_VIDEO_MODE_H264;
    if (strcmp(g_prefVideoMode, "h265") == 0) return STR_VIDEO_MODE_H265;
    if (strcmp(g_prefVideoMode, "legacy") == 0) return STR_VIDEO_MODE_LEGACY;
    return STR_VIDEO_MODE_TILES;
}

/* GBA/mGBA's actual native audio rate (matches Dolphin fork's
 * GBAStreamHost's own m_audio_sample_rate default, GBAStreamHost.h) --
 * fixed at maxmod stream-open time (main()), not per-session, since
 * GBAStreamHandshake.cpp's NegotiateAudio() never downsamples (rate always
 * stays native, see its own comment), so every session's audio.sample_rate
 * is this same constant anyway. */
#define AUDIO_SAMPLE_RATE 32768

/* ~0.5s of mono 16-bit audio at AUDIO_SAMPLE_RATE -- a jitter buffer
 * against this link's bursty delivery (see RECV_BUF_CAP's own comment),
 * not a lot of RAM on a 4MB console. Written by the network receive loop
 * in runSession() as FINLINK_MSG_AUDIO frames arrive, read by
 * audioStreamRequest() (maxmod's pull callback, invoked from
 * mmStreamUpdate() -- called once per tick from the *same* thread as the
 * network loop, so unlike g_recvBuf there's no real concurrency here
 * despite two "sides" touching this buffer, and no locking is needed. */
#define AUDIO_RING_SAMPLES (AUDIO_SAMPLE_RATE / 2)
static int16_t g_audioRing[AUDIO_RING_SAMPLES];
static size_t g_audioWritePos = 0;
static size_t g_audioReadPos = 0;
static size_t g_audioAvailable = 0; /* samples currently buffered, <= AUDIO_RING_SAMPLES */

/* Drops leftover buffered audio from a previous session -- called at the
 * start of each new runSession() so stale samples from before a
 * reconnect can't play at the start of a new one. */
static void audioRingReset(void) {
    g_audioWritePos = 0;
    g_audioReadPos = 0;
    g_audioAvailable = 0;
}

static void audioRingPush(const uint8_t *samplesS16le, size_t sampleCount) {
    for (size_t i = 0; i < sampleCount; i++) {
        if (g_audioAvailable >= AUDIO_RING_SAMPLES) {
            break; /* overflow: drop the tail rather than overwrite not-yet-played samples */
        }
        g_audioRing[g_audioWritePos] = finlink_read_s16le(samplesS16le + i * 2);
        g_audioWritePos = (g_audioWritePos + 1) % AUDIO_RING_SAMPLES;
        g_audioAvailable++;
    }
}

/* maxmod's pull callback (mm_stream.callback, maxmod9.h) -- must always
 * fill exactly `length` samples. Pads with silence on underrun (network
 * hasn't delivered enough yet) rather than replaying stale samples or
 * leaving the destination buffer uninitialized. */
static mm_word audioStreamRequest(mm_word length, mm_addr dest, mm_stream_formats format) {
    (void)format; /* always MM_STREAM_16BIT_MONO here, see main()'s mmStreamOpen() call */
    int16_t *out = (int16_t *)dest;
    size_t take = (length < g_audioAvailable) ? length : g_audioAvailable;
    for (size_t i = 0; i < take; i++) {
        out[i] = g_audioRing[g_audioReadPos];
        g_audioReadPos = (g_audioReadPos + 1) % AUDIO_RING_SAMPLES;
    }
    for (size_t i = take; i < length; i++) {
        out[i] = 0;
    }
    g_audioAvailable -= take;
    return length;
}

static void recvBufConsume(size_t n) {
    memmove(g_recvBuf, g_recvBuf + n, g_recvLen - n);
    g_recvLen -= n;
}

static void weakRandomBytes(uint8_t *out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        out[i] = (uint8_t)(rand() & 0xFF);
    }
}

static inline uint16_t rgb565ToNds(uint16_t px) {
    uint8_t r = (px >> 11) & 0x1F;
    uint8_t g = (px >> 5) & 0x3F;
    uint8_t b = px & 0x1F;
    return RGB15(r, g >> 1, b) | BIT(15);
}

/* 2D bilinear blend of the four RGB565 pixels surrounding a fractional
 * source position, wx/wy each 0..255 fixed-point (256 == "fully the next
 * pixel", only reachable at the rightmost/bottommost edge where p10==p00
 * or p01==p00 anyway -- blending a pixel with itself is a no-op). Unpacks
 * each 5-6-5 channel, interpolates independently, repacks -- multiplies
 * and shifts only, no division (see blitFrameScaled()'s own comment on
 * ARM9 having no hardware divider). */
static uint16_t bilinearBlend(uint16_t p00, uint16_t p10, uint16_t p01, uint16_t p11, int wx, int wy) {
    int r00 = (p00 >> 11) & 0x1F, g00 = (p00 >> 5) & 0x3F, b00 = p00 & 0x1F;
    int r10 = (p10 >> 11) & 0x1F, g10 = (p10 >> 5) & 0x3F, b10 = p10 & 0x1F;
    int r01 = (p01 >> 11) & 0x1F, g01 = (p01 >> 5) & 0x3F, b01 = p01 & 0x1F;
    int r11 = (p11 >> 11) & 0x1F, g11 = (p11 >> 5) & 0x3F, b11 = p11 & 0x1F;

    int r0 = r00 + (((r10 - r00) * wx) >> 8);
    int r1 = r01 + (((r11 - r01) * wx) >> 8);
    int r = r0 + (((r1 - r0) * wy) >> 8);

    int g0 = g00 + (((g10 - g00) * wx) >> 8);
    int g1 = g01 + (((g11 - g01) * wx) >> 8);
    int g = g0 + (((g1 - g0) * wy) >> 8);

    int b0 = b00 + (((b10 - b00) * wx) >> 8);
    int b1 = b01 + (((b11 - b01) * wx) >> 8);
    int b = b0 + (((b1 - b0) * wy) >> 8);

    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* Uniform-scale variant of blitFrame() below -- see g_prefAspectScale's own
 * comment for the exact target size (256x170, screen-width-bound). Nearest-
 * neighbor (g_prefBilinear off, the default): column source indices only
 * depend on x (not y), so they're precomputed once per call into colSrc[]
 * instead of one integer division per pixel -- ARM9 has no hardware
 * divider, so this turns ~43,520 divisions into ~426. Bilinear
 * (g_prefBilinear on) follows the identical "precompute the per-column
 * part once, not per pixel" budget, just with a source pixel pair +
 * fixed-point weight per column instead of a single index -- still no
 * per-pixel division, see bilinearBlend() above. */
static void blitFrameScaled(const uint8_t *rgb565) {
    const int scaledW = 256;
    const int scaledH = (GBA_H * scaledW) / GBA_W; /* 170 */
    const int offY = (192 - scaledH) / 2;

    if (g_prefBilinear) {
        static int colSrc0[256], colSrc1[256], colWeight[256];
        for (int x = 0; x < scaledW; x++) {
            int srcXFixed = (x * GBA_W * 256) / scaledW;
            colSrc0[x] = srcXFixed >> 8;
            colSrc1[x] = colSrc0[x] + 1 < GBA_W ? colSrc0[x] + 1 : colSrc0[x];
            colWeight[x] = srcXFixed & 0xFF;
        }
        for (int y = 0; y < scaledH; y++) {
            int srcYFixed = (y * GBA_H * 256) / scaledH;
            int srcY0 = srcYFixed >> 8;
            int srcY1 = srcY0 + 1 < GBA_H ? srcY0 + 1 : srcY0;
            int wY = srcYFixed & 0xFF;
            const uint8_t *row0 = rgb565 + (size_t)srcY0 * GBA_W * 2;
            const uint8_t *row1 = rgb565 + (size_t)srcY1 * GBA_W * 2;
            uint16_t *dstRow = VRAM_A + (size_t)(y + offY) * 256;
            for (int x = 0; x < scaledW; x++) {
                uint16_t p00 = finlink_read_u16le(row0 + (size_t)colSrc0[x] * 2);
                uint16_t p10 = finlink_read_u16le(row0 + (size_t)colSrc1[x] * 2);
                uint16_t p01 = finlink_read_u16le(row1 + (size_t)colSrc0[x] * 2);
                uint16_t p11 = finlink_read_u16le(row1 + (size_t)colSrc1[x] * 2);
                dstRow[x] = rgb565ToNds(bilinearBlend(p00, p10, p01, p11, colWeight[x], wY));
            }
        }
        return;
    }

    static int colSrc[256];
    for (int x = 0; x < scaledW; x++) {
        colSrc[x] = (x * GBA_W) / scaledW;
    }

    for (int y = 0; y < scaledH; y++) {
        const int srcY = (y * GBA_H) / scaledH;
        const uint8_t *srcRow = rgb565 + (size_t)srcY * GBA_W * 2;
        uint16_t *dstRow = VRAM_A + (size_t)(y + offY) * 256;
        for (int x = 0; x < scaledW; x++) {
            dstRow[x] = rgb565ToNds(finlink_read_u16le(srcRow + (size_t)colSrc[x] * 2));
        }
    }
}

/* Draws framebuffer_rgb565 (GBA_W x GBA_H, row-major u16le RGB565) into
 * VRAM_A, which main() put in MODE_FB0 (direct pixel display, see
 * nds/arm9/video.h) -- converts each pixel from the wire's 5-6-5 RGB to the
 * NDS framebuffer's 5-5-5 RGB + the mandatory bit-15 "opaque" flag (an
 * FB-mode pixel with bit 15 clear is treated as transparent/shows nothing).
 * Centered 1:1 by default, or uniformly upscaled (blitFrameScaled()) if the
 * user opted into that via g_prefAspectScale. */
static void blitFrame(const uint8_t *rgb565) {
    if (g_prefAspectScale) {
        blitFrameScaled(rgb565);
        return;
    }
    const int offX = (256 - GBA_W) / 2;
    const int offY = (192 - GBA_H) / 2;
    for (int y = 0; y < GBA_H; y++) {
        const uint8_t *srcRow = rgb565 + (size_t)y * GBA_W * 2;
        uint16_t *dstRow = VRAM_A + (size_t)(y + offY) * 256 + offX;
        for (int x = 0; x < GBA_W; x++) {
            dstRow[x] = rgb565ToNds(finlink_read_u16le(srcRow + (size_t)x * 2));
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
                                 size_t outReasonCap, char outStreamType[FINLINK_STREAM_TYPE_LEN],
                                 const char *videoMode, char outGrantedVideoMode[FINLINK_VIDEO_MODE_LEN]) {
    outReason[0] = '\0';
    outStreamType[0] = '\0';
    outGrantedVideoMode[0] = '\0';

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
        /* Mono explicitly (not hello.audio.channels, typically stereo) --
         * this link is the tight resource (see this file's top comment),
         * and GBAStreamHost.cpp's NegotiateAudio() honors max_channels via
         * its existing stereo->mono downmix. max_sample_rate is sent for
         * protocol completeness even though NegotiateAudio() never
         * actually downsamples (see AUDIO_SAMPLE_RATE's own comment). */
        ackReq.wants_audio = 1;
        ackReq.max_sample_rate = hello.has_audio && hello.audio.sample_rate > 0 ? hello.audio.sample_rate
                                                                                 : AUDIO_SAMPLE_RATE;
        ackReq.max_channels = 1;
        strncpy(ackReq.video_mode, videoMode, sizeof(ackReq.video_mode) - 1);

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
            strncpy(outGrantedVideoMode, ready.video_mode, FINLINK_VIDEO_MODE_LEN - 1);
            outGrantedVideoMode[FINLINK_VIDEO_MODE_LEN - 1] = '\0';
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
        iprintf("%s\n", STR_STATUS_CONNECT_FAILED);
        iprintf("\n%s\n", STR_PRESS_KEY_FOR_MENU);
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
    char grantedVideoMode[FINLINK_VIDEO_MODE_LEN];
    if (!performAppHandshake(&fd, host, sizeof(host), &port, handshakeFailReason, sizeof(handshakeFailReason),
                              streamType, g_prefVideoMode, grantedVideoMode)) {
        if (fd >= 0) {
            closesocket(fd);
        }
        iprintf(STR_HANDSHAKE_FAILED, handshakeFailReason);
        iprintf("\n");
        iprintf("\n%s\n", STR_PRESS_KEY_FOR_MENU);
        while (true) {
            swiWaitForVBlank();
            scanKeys();
            if (keysDown()) {
                return;
            }
        }
    }

    /* Empty grantedVideoMode means the server predates session_ready.
     * video_mode entirely -- skip the comparison rather than assuming
     * "tiles" was granted, see docs/protocol.md "Video-mode fallback".
     * Blocking (this whole client is synchronous, no background thread to
     * race with) -- A continues into the session below, B disconnects and
     * returns to the menu same as any other abort path in this function. */
    if (grantedVideoMode[0] != '\0' && strcmp(grantedVideoMode, g_prefVideoMode) != 0) {
        const char *requestedLabel = videoModePrefLabel();
        const char *grantedLabel = strcmp(grantedVideoMode, "h264") == 0 ? STR_VIDEO_MODE_H264
                                  : strcmp(grantedVideoMode, "h265") == 0 ? STR_VIDEO_MODE_H265
                                  : strcmp(grantedVideoMode, "legacy") == 0 ? STR_VIDEO_MODE_LEGACY
                                                                            : STR_VIDEO_MODE_TILES;
        consoleClear();
        iprintf("%s\n\n", STR_VIDEO_MODE_FALLBACK_TITLE);
        iprintf(STR_VIDEO_MODE_FALLBACK_MESSAGE, requestedLabel, grantedLabel);
        iprintf("\n\n");
        iprintf(" A = %s\n", STR_VIDEO_MODE_FALLBACK_CONTINUE);
        iprintf(" B = %s\n", STR_VIDEO_MODE_FALLBACK_ABORT);
        for (;;) {
            swiWaitForVBlank();
            scanKeys();
            int keys = keysDown();
            if (keys & KEY_A) {
                break;
            }
            if (keys & KEY_B) {
                closesocket(fd);
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

    /* blitFrame()'s two modes (centered 1:1 vs. aspect-scaled) draw
     * different-sized regions of VRAM_A -- re-clear it here so a mode
     * switched since the last session (or main()'s own one-time startup
     * clear, for the very first session) doesn't leave stale border pixels
     * from the other mode's differently-sized draw region. */
    memset(VRAM_A, 0, 256 * 192 * 2);
    audioRingReset();

    consoleClear();
    iprintf("%s. %s\n\n", STR_STATUS_CONNECTED, STR_EXIT_HOLD_HINT);
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
        /* Manual-mode maxmod stream (main()'s mmStreamOpen() call) --
         * pulls from g_audioRing via audioStreamRequest() as needed, same
         * once-per-tick call site as examples/nds/audio/maxmod/streaming's
         * own reference loop. */
        mmStreamUpdate();
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
                                    /* Re-send input right after every decoded
                                     * video frame, not just once per outer
                                     * loop iteration (see the top of this
                                     * loop) -- a burst of several video
                                     * frames arriving back-to-back (e.g.
                                     * after a brief WiFi stall) used to be
                                     * decoded here in full, with input never
                                     * re-scanned until the whole backlog was
                                     * drained; a quick press+release entirely
                                     * within that window was silently never
                                     * sent at all, not just delayed --
                                     * scanKeys() only captures state at the
                                     * moment it's called. This doesn't wait
                                     * for the next real hardware vblank
                                     * (unlike the outer loop's own
                                     * swiWaitForVBlank()), so it costs
                                     * nothing when there's no backlog: keys
                                     * only actually change once per real
                                     * vblank regardless of how often
                                     * scanKeys() itself is called. */
                                    scanKeys();
                                    sendGbaInput(fd, buildGbaKeyMask(keysHeld()));
                                } else {
                                    window.decodeErrors++;
                                    totalDecodeErrors++;
                                }
                            }
                        }
                    } else if (type == FINLINK_MSG_AUDIO) {
                        finlink_audio_frame audioFrame;
                        if (finlink_parse_audio_frame(frame.payload, frame.payload_size, &audioFrame) ==
                            FINLINK_OK) {
                            window.audioFrames++;
                            window.audioBytes += (unsigned)frame.payload_size;
                            /* Mono was requested (performAppHandshake()), so
                             * sample_count here is already mono sample
                             * count, matching g_audioRing 1:1 -- no
                             * downmixing needed client-side. */
                            audioRingPush(audioFrame.samples, audioFrame.sample_count);
                        }
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
        iprintf("\n");
        iprintf(STR_STATUS_DISCONNECTED_REASON, disconnectReason);
        iprintf("\n");
    } else {
        iprintf("\n%s\n", STR_STATUS_DISCONNECTED);
    }
    iprintf("%s\n", STR_PRESS_KEY_FOR_MENU);
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
/* outIp must be at least 16 bytes (serverIp at both call sites in main()
 * is FINLINK_BEACON_HOST_LEN, comfortably larger) -- 15 chars is enough
 * for the longest dotted-decimal IPv4 address plus a NUL, and the width
 * limit below matches that exactly so there's no separate truncating copy
 * to get wrong. */
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
    iprintf("%s NDS - Machbarkeitstest\n\n", STR_APP_NAME);
    iprintf("%s\n", STR_IP_PROMPT_TITLE);
    iprintf("(%s)\n\n", STR_HOST_HINT_EXAMPLE);
    outIp[0] = '\0';
    iscanf("%15s", outIp);
}

/* Only GC_GBA_LINK (Dolphin) is multi-slot -- every other stream type is
 * single-client and connects straight to the beacon's own handshake_port
 * instead (docs/protocol.md), matching clients/android's MenuActivity.kt,
 * clients/switch's menu_activity.cpp, and clients/3ds's main.cpp, all of
 * which gate their P1-P4-equivalent picker the same way. An empty
 * streamType means "unknown" (manual IP entry has no beacon to read a
 * stream_type from) -- treated as GC_GBA_LINK, same fallback the other
 * three clients' own manual-entry paths already accept. */
static bool streamTypeIsMultiSlot(const char *streamType) {
    return streamType[0] == '\0' || strcmp(streamType, "GC_GBA_LINK") == 0;
}

/* Cursor-navigable list (UP/DOWN + A to confirm, B to cancel) -- this
 * client's menus are entirely button-driven, not touch-driven, so this is
 * the closest equivalent to every other client's "tap a row, pick from it,
 * land back where you were" language screen. Confirming applies the choice
 * immediately (every STR_* read from here on already reflects it, applied
 * via applyLanguage()) and returns to the caller, which re-renders itself
 * fully to pick up the new language on every line, not just this one. */
static void languageMenu(void) {
    struct LanguageOption {
        int prefValue;
        const char *label;
    };
    const int kOptionCount = 6;
    struct LanguageOption options[6] = {
        { -1, STR_LANGUAGE_SYSTEM },
        { STR_LANG_DE, STR_LANGUAGE_GERMAN },
        { STR_LANG_EN, STR_LANGUAGE_ENGLISH },
        { STR_LANG_FR, STR_LANGUAGE_FRENCH },
        { STR_LANG_IT, STR_LANGUAGE_ITALIAN },
        { STR_LANG_ES, STR_LANGUAGE_SPANISH },
    };
    /* Sorted by the displayed label, not a fixed order -- "System" is
     * localized like any other UI string, but every language name itself
     * is a fixed endonym (see strings.json), so this only actually
     * reorders relative to "System"/"Systeme"/... as more languages are
     * added later. Plain insertion sort -- only six entries, not worth
     * pulling in qsort() for. */
    for (int i = 1; i < kOptionCount; i++) {
        struct LanguageOption key = options[i];
        int j = i - 1;
        while (j >= 0 && strcmp(options[j].label, key.label) > 0) {
            options[j + 1] = options[j];
            j--;
        }
        options[j + 1] = key;
    }

    int cursor = 0;
    for (int i = 0; i < kOptionCount; i++) {
        if (options[i].prefValue == g_prefLanguage) {
            cursor = i;
        }
    }

    bool dirty = true;
    for (;;) {
        if (dirty) {
            consoleClear();
            iprintf("%s\n\n", STR_SETTINGS_LANGUAGE);
            for (int i = 0; i < kOptionCount; i++) {
                iprintf("%s %s\n", i == cursor ? ">" : " ", options[i].label);
            }
            dirty = false;
        }
        swiWaitForVBlank();
        scanKeys();
        int keys = keysDown();
        if (keys & KEY_UP) {
            cursor = (cursor + kOptionCount - 1) % kOptionCount;
            dirty = true;
        } else if (keys & KEY_DOWN) {
            cursor = (cursor + 1) % kOptionCount;
            dirty = true;
        } else if (keys & KEY_A) {
            g_prefLanguage = options[cursor].prefValue;
            applyLanguage();
            return;
        } else if (keys & KEY_B) {
            return;
        }
    }
}

/* Same cursor-navigable "pick a row, land back where you were" flow as
 * languageMenu() above, cloned wholesale. NOT alphabetically sorted,
 * unlike languageMenu()'s options -- deliberate order (default first, then
 * the two stronger/lossier compressed options, legacy last as the
 * explicit-opt-out fallback), same as Android's Prefs.VIDEO_MODES. */
static void videoModeMenu(void) {
    struct VideoModeOption {
        const char *value; /* wire-format string, see finlink/docs/protocol.md */
        const char *label;
    };
    const int kOptionCount = 4;
    struct VideoModeOption options[4] = {
        { "tiles", STR_VIDEO_MODE_TILES },
        { "h264", STR_VIDEO_MODE_H264 },
        { "h265", STR_VIDEO_MODE_H265 },
        { "legacy", STR_VIDEO_MODE_LEGACY },
    };

    int cursor = 0;
    for (int i = 0; i < kOptionCount; i++) {
        if (strcmp(options[i].value, g_prefVideoMode) == 0) {
            cursor = i;
        }
    }

    bool dirty = true;
    for (;;) {
        if (dirty) {
            consoleClear();
            iprintf("%s\n\n", STR_SETTINGS_VIDEO_MODE);
            for (int i = 0; i < kOptionCount; i++) {
                iprintf("%s %s\n", i == cursor ? ">" : " ", options[i].label);
            }
            dirty = false;
        }
        swiWaitForVBlank();
        scanKeys();
        int keys = keysDown();
        if (keys & KEY_UP) {
            cursor = (cursor + kOptionCount - 1) % kOptionCount;
            dirty = true;
        } else if (keys & KEY_DOWN) {
            cursor = (cursor + 1) % kOptionCount;
            dirty = true;
        } else if (keys & KEY_A) {
            strncpy(g_prefVideoMode, options[cursor].value, sizeof(g_prefVideoMode) - 1);
            g_prefVideoMode[sizeof(g_prefVideoMode) - 1] = '\0';
            return;
        } else if (keys & KEY_B) {
            return;
        }
    }
}

/* Returns 0-3 for the chosen slot (GC_GBA_LINK) or 0 for "connect" (every
 * other stream type, see streamTypeIsMultiSlot()), -1 to quit, -2 to
 * re-run discovery, -3 to enter the server IP manually. */
static int slotSelectMenu(const char *ownIp, const char *serverIp, bool autoDiscovered,
                           const char *streamType) {
    const bool multiSlot = streamTypeIsMultiSlot(streamType);
    consoleClear();
    iprintf("%s NDS - Machbarkeitstest\n\n", STR_APP_NAME);
    iprintf("Server: %s%s\n", serverIp, autoDiscovered ? " (gefunden)" : " (manuell)");
    iprintf(STR_DISCOVERY_OWN_IP, ownIp);
    iprintf("\n\n");
    if (multiSlot) {
        iprintf("Slot waehlen:\n");
        iprintf(" A = Slot 1 (Port 6801)\n");
        iprintf(" B = Slot 2 (Port 6802)\n");
        iprintf(" X = Slot 3 (Port 6803)\n");
        iprintf(" Y = Slot 4 (Port 6804)\n\n");
    } else {
        /* Single-client stream types have nothing to pick a slot among --
         * probing PLAYER_BASE_PORT+0..3 against a server that was never
         * Dolphin doesn't find "free"/"occupied" slots, just four
         * unreachable ports, so this doesn't show that picker at all. */
        iprintf("%s\n\n\n\n\n", STR_CONNECT_HINT_SLOT);
    }
    iprintf(" %s\n", STR_CONNECT_HINT_RESEARCH);
    iprintf(" %s\n", STR_CONNECT_HINT_MANUAL);
    iprintf(" L = %s\n", STR_SCREEN_TOGGLE);
    iprintf(" HOCH = %s\n", STR_SCALE_TOGGLE);
    iprintf(" RUNTER = %s\n", STR_FILTER_TOGGLE);
    iprintf(" LINKS = %s\n", STR_SETTINGS_LANGUAGE);
    iprintf(" RECHTS = %s\n", STR_SETTINGS_VIDEO_MODE);
    iprintf(" %s\n\n", STR_CONNECT_HINT_QUIT);
    /* Only affects single-screen stream types (GC_GBA_LINK today) -- a
     * stream_type that's itself a dual-screen source's secondary screen
     * always goes to this client's bottom screen regardless, so this
     * setting is only actually consulted in that case. See runSession(). */
    iprintf("\x1b[17;0HBildschirm: %s   ", g_prefBottomScreen ? STR_SCREEN_BOTTOM : STR_SCREEN_TOP);
    iprintf("\x1b[18;0HSkalierung: %s   ", g_prefAspectScale ? STR_SCALE_FILL : STR_SCALE_1TO1);
    // Only actually visible when Skalierung is "ausgefuellt" -- centered
    // 1:1 has no scaling to filter, see g_prefBilinear's own comment.
    iprintf("\x1b[19;0HFilterung: %s   ", g_prefBilinear ? STR_FILTER_ON : STR_FILTER_OFF);
    iprintf("\x1b[20;0H%s: %s   ", STR_SETTINGS_LANGUAGE, languagePrefLabel());
    iprintf("\x1b[21;0H%s: %s   ", STR_SETTINGS_VIDEO_MODE, videoModePrefLabel());

    for (;;) {
        swiWaitForVBlank();
        scanKeys();
        int keys = keysDown();
        if (keys & KEY_A) return 0;
        if (multiSlot) {
            if (keys & KEY_B) return 1;
            if (keys & KEY_X) return 2;
            if (keys & KEY_Y) return 3;
        }
        if (keys & KEY_SELECT) return -2;
        if (keys & KEY_R) return -3;
        if (keys & KEY_L) {
            g_prefBottomScreen = !g_prefBottomScreen;
            iprintf("\x1b[17;0HBildschirm: %s   ", g_prefBottomScreen ? STR_SCREEN_BOTTOM : STR_SCREEN_TOP);
        }
        if (keys & KEY_UP) {
            g_prefAspectScale = !g_prefAspectScale;
            iprintf("\x1b[18;0HSkalierung: %s   ", g_prefAspectScale ? STR_SCALE_FILL : STR_SCALE_1TO1);
        }
        if (keys & KEY_DOWN) {
            g_prefBilinear = !g_prefBilinear;
            iprintf("\x1b[19;0HFilterung: %s   ", g_prefBilinear ? STR_FILTER_ON : STR_FILTER_OFF);
        }
        if (keys & KEY_LEFT) {
            languageMenu();
            /* Every STR_* line already printed above this loop (hints,
             * headers, ...) needs to change too, not just this row -- a
             * full re-render is simplest on a sequential iprintf console,
             * unlike the single-line ANSI-positioned patches above. */
            return slotSelectMenu(ownIp, serverIp, autoDiscovered, streamType);
        }
        if (keys & KEY_RIGHT) {
            videoModeMenu();
            /* Same full-re-render reasoning as KEY_LEFT above. */
            return slotSelectMenu(ownIp, serverIp, autoDiscovered, streamType);
        }
        if (keys & KEY_START) return -1;
    }
}

/* Redraws only the live server-list region (rows 5..5+BEACON_MAX_SERVERS)
 * of serverSelectMenu()'s screen -- called once per tick, not the whole
 * screen, since that's the only part that ever changes while it's shown
 * (a beacon arriving/going stale), matching this file's existing
 * ANSI-cursor-positioned partial-update convention (e.g. runSession()'s
 * own stats lines) instead of a full consoleClear() 60 times/sec. */
static void drawServerList(const BeaconScan *scan) {
    static const char *labels[BEACON_MAX_SERVERS] = { "A", "B", "X", "Y" };
    int shown = 0;
    for (int i = 0; i < BEACON_MAX_SERVERS; i++) {
        iprintf("\x1b[%d;0H", 5 + i);
        if (scan->servers[i].inUse) {
            const finlink_beacon *b = &scan->servers[i].beacon;
            iprintf(" %s = %s%s                              \n", labels[i],
                     b->game_title[0] ? b->game_title : b->host,
                     scan->servers[i].compatible ? "" : STR_DISCOVERY_INCOMPATIBLE_SUFFIX);
            shown++;
        } else {
            iprintf("                                              \n");
        }
    }
    // %-40s: left-justified, space-padded to 40 columns regardless of the
    // string's actual length -- STR_DISCOVERY_SEARCHING_PLACEHOLDER is a
    // different length than the hand-counted-spaces literal this replaced,
    // so a fixed count of trailing spaces would no longer reliably clear
    // the row when this line's content shrinks back down.
    iprintf("\x1b[%d;0H%-40s\n", 5 + BEACON_MAX_SERVERS, shown == 0 ? STR_DISCOVERY_SEARCHING_PLACEHOLDER : "");
}

/* Live UDP-beacon server picker (beacon_discovery.h/.c), replacing the old
 * subnet-sweep runDiscovery() now that a server announces itself
 * periodically instead of needing to be found by probing every host on
 * the /24 (docs/protocol.md "Discovery-Beacon (UDP)"). *scan must already
 * be running (beaconScan_start(), called once in main() -- this only
 * ticks/displays it, so re-entering this screen from slotSelectMenu()'s
 * SELECT doesn't lose anything already heard). Returns 0-3 for the chosen
 * (compatible) server, -1 to quit the app, -3 to enter the IP manually --
 * same convention as slotSelectMenu() below, whose R/START already mean
 * exactly that. outHost must be at least FINLINK_BEACON_HOST_LEN bytes;
 * outStreamType at least FINLINK_BEACON_STREAM_TYPE_LEN bytes -- fed
 * straight into slotSelectMenu()/main()'s port choice so a discovered
 * non-GC_GBA_LINK server never goes through the GC_GBA_LINK-only slot
 * picker (see streamTypeIsMultiSlot()). */
static int serverSelectMenu(BeaconScan *scan, const char *ownIp, char *outHost, size_t outHostCap,
                             char *outStreamType, int *outHandshakePort) {
    consoleClear();
    iprintf("%s NDS - Machbarkeitstest\n\n", STR_APP_NAME);
    iprintf(STR_DISCOVERY_OWN_IP, ownIp);
    iprintf("\n\n");
    iprintf("%s\n", STR_DISCOVERY_FOUND_HEADER);
    drawServerList(scan);
    iprintf("\x1b[%d;0H %s\n %s\n", 6 + BEACON_MAX_SERVERS, STR_CONNECT_HINT_MANUAL, STR_CONNECT_HINT_QUIT);

    for (;;) {
        swiWaitForVBlank();
        beaconScan_tick(scan);
        drawServerList(scan);
        scanKeys();
        int keys = keysDown();

        static const int keyBits[BEACON_MAX_SERVERS] = { KEY_A, KEY_B, KEY_X, KEY_Y };
        for (int i = 0; i < BEACON_MAX_SERVERS; i++) {
            if ((keys & keyBits[i]) && scan->servers[i].inUse && scan->servers[i].compatible) {
                strncpy(outHost, scan->servers[i].beacon.host, outHostCap - 1);
                outHost[outHostCap - 1] = '\0';
                strncpy(outStreamType, scan->servers[i].beacon.stream_type, FINLINK_BEACON_STREAM_TYPE_LEN - 1);
                outStreamType[FINLINK_BEACON_STREAM_TYPE_LEN - 1] = '\0';
                *outHandshakePort = scan->servers[i].beacon.handshake_port;
                return i;
            }
        }
        if (keys & KEY_R) {
            return -3;
        }
        if (keys & KEY_START) {
            return -1;
        }
    }
}

int main(void) {
    videoSetMode(MODE_FB0);
    vramSetBankA(VRAM_A_LCD);
    memset(VRAM_A, 0, 256 * 192 * 2);

    consoleDemoInit();
    keyboardDemoInit()->OnKeyPressed = onKeyboardKeyPressed; /* see promptForIp() */

    applyLanguage(); /* before any STR_* text is ever printed */

    /* No soundbank (no mod/sample playback, only the raw PCM stream below)
     * -- same "unusual setup" as examples/nds/audio/maxmod/streaming's own
     * reference. Opened once here, for the app's whole lifetime: the
     * ring buffer just plays silence between sessions (audioRingReset()
     * clears it at the start of each new one), so there's no need to
     * open/close per connection. */
    mm_ds_system mmSys;
    memset(&mmSys, 0, sizeof(mmSys));
    mmInit(&mmSys);

    mm_stream mmAudioStream;
    memset(&mmAudioStream, 0, sizeof(mmAudioStream));
    mmAudioStream.sampling_rate = AUDIO_SAMPLE_RATE;
    mmAudioStream.buffer_length = AUDIO_SAMPLE_RATE / 10; /* ~100ms hardware buffer */
    mmAudioStream.callback = audioStreamRequest;
    mmAudioStream.format = MM_STREAM_16BIT_MONO;
    mmAudioStream.manual = true; /* pulled explicitly via mmStreamUpdate(), see runSession()'s tick loop */
    mmStreamOpen(&mmAudioStream);

    iprintf("%s NDS - Machbarkeitstest\n\n", STR_APP_NAME);
    iprintf("%s\n", STR_WIFI_CONNECTING);

    if (!Wifi_InitDefault(WFC_CONNECT)) {
        iprintf("%s\n", STR_WIFI_FAILED);
        iprintf("%s\n", STR_WIFI_FAILED_HINT);
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

    /* Started once here (not per-menu-visit) so beacons already heard
     * aren't lost/re-waited-for each time the user returns to
     * serverSelectMenu() via slotSelectMenu()'s SELECT. */
    BeaconScan beaconScan;
    beaconScan_start(&beaconScan);

    char serverIp[FINLINK_BEACON_HOST_LEN];
    char serverStreamType[FINLINK_BEACON_STREAM_TYPE_LEN];
    int serverHandshakePort = 0;
    serverIp[0] = '\0';
    serverStreamType[0] = '\0';
    bool autoDiscovered =
        serverSelectMenu(&beaconScan, ownIp, serverIp, sizeof(serverIp), serverStreamType, &serverHandshakePort) >= 0;
    if (!autoDiscovered) {
        promptForIp(serverIp);
        serverStreamType[0] = '\0'; /* unknown -- see streamTypeIsMultiSlot() */
    }

    for (;;) {
        int slot = slotSelectMenu(ownIp, serverIp, autoDiscovered, serverStreamType);
        if (slot == -2) {
            autoDiscovered = serverSelectMenu(&beaconScan, ownIp, serverIp, sizeof(serverIp), serverStreamType,
                                               &serverHandshakePort) >= 0;
            if (!autoDiscovered) {
                promptForIp(serverIp);
                serverStreamType[0] = '\0';
            }
            continue;
        }
        if (slot == -3) {
            promptForIp(serverIp);
            autoDiscovered = false;
            serverStreamType[0] = '\0';
            continue;
        }
        if (slot < 0) {
            break;
        }
        /* GC_GBA_LINK dials the chosen player-slot port; every other
         * stream type is single-client and connects straight to the
         * beacon's own handshake_port instead (slotSelectMenu() only ever
         * returns 0 in that case, see streamTypeIsMultiSlot()). */
        int port = streamTypeIsMultiSlot(serverStreamType) ? kSlotPorts[slot] : serverHandshakePort;
        runSession(serverIp, port);
        /* runSession() may have swapped MAIN/SUB to the bottom screen (see
         * its own comment) -- every one of its exit paths returns here, so
         * resetting once, in this one place, is enough to keep every menu
         * screen back on its usual (top-screen-video) layout regardless of
         * how the session ended. */
        lcdMainOnTop();
    }

    beaconScan_stop(&beaconScan);
    return 0;
}
