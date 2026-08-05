#include "unison/protocol.h"
#include "unison/endian.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                     \
        }                                                                \
    } while (0)

static void test_peek_type(void) {
    unison_msg_type type;

    CHECK(unison_peek_type(NULL, 0, &type) == UNISON_ERR_TOO_SHORT);

    const uint8_t video_byte[] = {1};
    CHECK(unison_peek_type(video_byte, sizeof(video_byte), &type) == UNISON_OK);
    CHECK(type == UNISON_MSG_VIDEO);

    const uint8_t bogus_byte[] = {42};
    CHECK(unison_peek_type(bogus_byte, sizeof(bogus_byte), &type) == UNISON_ERR_UNKNOWN_TYPE);
}

static void test_video_header(void) {
    /* type=1, width=240, height=160, format=0 (full, non-indexed),
     * followed by 2 dummy compressed bytes */
    const uint8_t frame[] = {
        1,
        240, 0, 0, 0,
        160, 0, 0, 0,
        0, /* format = 0: full frame, raw RGB565 */
        0xAA, 0xBB
    };

    unison_video_header hdr;
    CHECK(unison_parse_video_header(frame, sizeof(frame), &hdr) == UNISON_OK);
    CHECK(hdr.width == 240);
    CHECK(hdr.height == 160);
    CHECK(hdr.format == 0);
    CHECK(hdr.compressed_size == 2);
    CHECK(hdr.compressed_data[0] == 0xAA);
    CHECK(hdr.compressed_data[1] == 0xBB);

    CHECK(unison_parse_video_header(frame, 9, &hdr) == UNISON_ERR_TOO_SHORT);

    /* format is parsed as a plain byte -- unison_decode_video_frame() is
     * what interprets the bits */
    const uint8_t indexed_frame[] = {
        1,
        4, 0, 0, 0,
        1, 0, 0, 0,
        UNISON_VIDEO_FORMAT_INDEXED | UNISON_VIDEO_FORMAT_TILES,
        0xCC
    };
    CHECK(unison_parse_video_header(indexed_frame, sizeof(indexed_frame), &hdr) == UNISON_OK);
    CHECK(hdr.format == (UNISON_VIDEO_FORMAT_INDEXED | UNISON_VIDEO_FORMAT_TILES));
    CHECK(hdr.compressed_size == 1);
}

static void test_max_inflated_size(void) {
    /* Always at least enough for a full non-indexed frame, the cheapest
     * upper bound any format could possibly need. */
    CHECK(unison_video_max_inflated_size(240, 160) > (size_t)240 * 160 * 2);
    /* Non-multiple-of-8 dimensions round the tile grid up, not down. */
    CHECK(unison_video_max_inflated_size(9, 9) >= (size_t)9 * 9 * 2 + 4 + 4 * 2);
}

static void test_decode_video_frame_full_raw(void) {
    /* 2x1 raw RGB565: pixel 0 = 0x1234, pixel 1 = 0x5678 (both LE) */
    const uint8_t inflated[] = { 0x34, 0x12, 0x78, 0x56 };
    uint8_t out[4];

    CHECK(unison_decode_video_frame(0, inflated, sizeof(inflated), 2, 1, out, sizeof(out)) == UNISON_OK);
    CHECK(unison_read_u16le(out) == 0x1234);
    CHECK(unison_read_u16le(out + 2) == 0x5678);

    /* Not enough inflated data for the promised width*height */
    CHECK(unison_decode_video_frame(0, inflated, 2, 2, 1, out, sizeof(out)) == UNISON_ERR_TOO_SHORT);

    /* Not enough room in the caller's framebuffer */
    CHECK(unison_decode_video_frame(0, inflated, sizeof(inflated), 2, 1, out, 3) == UNISON_ERR_TOO_SHORT);
}

static void test_decode_video_frame_full_indexed(void) {
    /* 2x2 indexed frame, 2-color palette: color 0 = 0x1111, color 1 =
     * 0x2222; pixel indices (row-major): 0, 1, 1, 0 */
    const uint8_t inflated[] = {
        2, 0,             /* palette_count = 2 */
        0x11, 0x11,       /* palette[0] */
        0x22, 0x22,       /* palette[1] */
        0, 1, 1, 0        /* indices, one per pixel */
    };
    uint8_t out[8];

    CHECK(unison_decode_video_frame(UNISON_VIDEO_FORMAT_INDEXED, inflated, sizeof(inflated), 2, 2, out,
                                      sizeof(out)) == UNISON_OK);
    CHECK(unison_read_u16le(out + 0) == 0x1111);
    CHECK(unison_read_u16le(out + 2) == 0x2222);
    CHECK(unison_read_u16le(out + 4) == 0x2222);
    CHECK(unison_read_u16le(out + 6) == 0x1111);

    /* Index pointing outside the palette it came with */
    const uint8_t bad_index[] = { 1, 0, 0x11, 0x11, 5 };
    uint8_t small_out[2];
    CHECK(unison_decode_video_frame(UNISON_VIDEO_FORMAT_INDEXED, bad_index, sizeof(bad_index), 1, 1, small_out,
                                      sizeof(small_out)) == UNISON_ERR_TOO_SHORT);

    /* Unknown bit set (only bits 0 and 1 are defined) */
    CHECK(unison_decode_video_frame(4, inflated, sizeof(inflated), 2, 2, out, sizeof(out)) ==
          UNISON_ERR_UNKNOWN_FORMAT);
}

static void fill_u16le(uint8_t *buf, size_t count, uint16_t value) {
    for (size_t i = 0; i < count; i++) {
        unison_write_u16le(buf + i * 2, value);
    }
}

static void test_decode_video_frame_tiles(void) {
    /* 16x8 framebuffer = 2x1 tiles (tile 0 = left half, tile 1 = right
     * half). Paint a full raw frame first, then a TILES-only patch that
     * only touches tile 1, and confirm tile 0's pixels are untouched --
     * this is the whole point of TILES: framebuffer_rgb565 is persistent
     * across calls, not reset by unison_decode_video_frame() itself. */
    const uint32_t width = 16, height = 8;
    uint8_t framebuffer[16 * 8 * 2];

    uint8_t full_frame[16 * 8 * 2];
    fill_u16le(full_frame, 16 * 8, 0xAAAA);
    CHECK(unison_decode_video_frame(0, full_frame, sizeof(full_frame), width, height, framebuffer,
                                      sizeof(framebuffer)) == UNISON_OK);
    for (size_t i = 0; i < 16 * 8; i++) {
        CHECK(unison_read_u16le(framebuffer + i * 2) == 0xAAAA);
    }

    uint8_t patch[2 + 1 * 2 + 64 * 2];
    unison_write_u16le(patch, 1);      /* tile_count = 1 */
    unison_write_u16le(patch + 2, 1);  /* tile_index = 1 (the right tile) */
    fill_u16le(patch + 4, 64, 0xBBBB);
    CHECK(unison_decode_video_frame(UNISON_VIDEO_FORMAT_TILES, patch, sizeof(patch), width, height, framebuffer,
                                      sizeof(framebuffer)) == UNISON_OK);

    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            uint16_t expected = x < 8 ? 0xAAAA : 0xBBBB;
            CHECK(unison_read_u16le(framebuffer + (y * width + x) * 2) == expected);
        }
    }

    /* Corrupt tile index (only 2 tiles exist, 0-1) */
    uint8_t bad_patch[2 + 1 * 2];
    unison_write_u16le(bad_patch, 1);
    unison_write_u16le(bad_patch + 2, 99);
    CHECK(unison_decode_video_frame(UNISON_VIDEO_FORMAT_TILES, bad_patch, sizeof(bad_patch), width, height,
                                      framebuffer, sizeof(framebuffer)) == UNISON_ERR_TOO_SHORT);
}

static void test_decode_video_frame_tiles_indexed(void) {
    /* Same 16x8 layout, but the tile patch is palette-indexed: 1-color
     * palette (0xCCCC), all 64 pixels use index 0. */
    const uint32_t width = 16, height = 8;
    uint8_t framebuffer[16 * 8 * 2];
    fill_u16le(framebuffer, 16 * 8, 0x0000);

    uint8_t patch[2 + 1 * 2 + 2 + 1 * 2 + 64];
    size_t off = 0;
    unison_write_u16le(patch + off, 1);
    off += 2; /* tile_count */
    unison_write_u16le(patch + off, 0);
    off += 2; /* tile_index = 0 (left tile) */
    unison_write_u16le(patch + off, 1);
    off += 2; /* palette_count */
    unison_write_u16le(patch + off, 0xCCCC);
    off += 2; /* palette[0] */
    memset(patch + off, 0, 64); /* every pixel uses palette index 0 */

    uint8_t format = UNISON_VIDEO_FORMAT_TILES | UNISON_VIDEO_FORMAT_INDEXED;
    CHECK(unison_decode_video_frame(format, patch, sizeof(patch), width, height, framebuffer,
                                      sizeof(framebuffer)) == UNISON_OK);

    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            uint16_t expected = x < 8 ? 0xCCCC : 0x0000;
            CHECK(unison_read_u16le(framebuffer + (y * width + x) * 2) == expected);
        }
    }
}

static void test_audio_frame(void) {
    /* type=3, sampleRate=32000, channels=2, 2 samples (4 bytes): -1, 1000 */
    const uint8_t frame[] = {
        3,
        0x00, 0x7D, 0x00, 0x00, /* 32000 LE */
        2,
        0xFF, 0xFF, /* -1 */
        0xE8, 0x03  /* 1000 */
    };

    unison_audio_frame audio;
    CHECK(unison_parse_audio_frame(frame, sizeof(frame), &audio) == UNISON_OK);
    CHECK(audio.sample_rate == 32000);
    CHECK(audio.channels == 2);
    CHECK(audio.sample_count == 2);
    CHECK(unison_read_s16le(audio.samples) == -1);
    CHECK(unison_read_s16le(audio.samples + 2) == 1000);
}

static void test_build_input_frame(void) {
    uint8_t buf[UNISON_INPUT_FRAME_SIZE];
    uint16_t mask = UNISON_KEY_A | UNISON_KEY_START;

    CHECK(unison_build_input_frame(mask, buf) == UNISON_INPUT_FRAME_SIZE);
    CHECK(buf[0] == UNISON_MSG_INPUT);
    CHECK(unison_read_u16le(buf + 1) == mask);
}

static void test_touch_frame_pressed(void) {
    uint8_t buf[UNISON_TOUCH_FRAME_SIZE];
    unison_touch_state touch;
    touch.pressed = 1;
    touch.x = 160;
    touch.y = 120;

    CHECK(unison_build_touch_frame(&touch, buf) == UNISON_TOUCH_FRAME_SIZE);
    CHECK(buf[0] == UNISON_MSG_INPUT);
    CHECK(buf[1] == 1);
    CHECK(unison_read_u16le(buf + 2) == 160);
    CHECK(unison_read_u16le(buf + 4) == 120);

    unison_touch_state parsed;
    CHECK(unison_parse_touch_frame(buf, sizeof(buf), &parsed) == UNISON_OK);
    CHECK(parsed.pressed != 0);
    CHECK(parsed.x == 160);
    CHECK(parsed.y == 120);
}

static void test_touch_frame_released(void) {
    /* A release must always encode x/y as 0 regardless of what the caller
     * passes in, per unison_touch_state's own comment -- a release has no
     * meaningful position. */
    uint8_t buf[UNISON_TOUCH_FRAME_SIZE];
    unison_touch_state touch;
    touch.pressed = 0;
    touch.x = 999;
    touch.y = 999;

    CHECK(unison_build_touch_frame(&touch, buf) == UNISON_TOUCH_FRAME_SIZE);
    CHECK(buf[1] == 0);
    CHECK(unison_read_u16le(buf + 2) == 0);
    CHECK(unison_read_u16le(buf + 4) == 0);

    unison_touch_state parsed;
    CHECK(unison_parse_touch_frame(buf, sizeof(buf), &parsed) == UNISON_OK);
    CHECK(parsed.pressed == 0);
}

static void test_extended_input_frame_pressed(void) {
    /* n3ds_touch_and_buttons: touch + a button superset + both analog
     * sticks in one fixed frame -- round-trip every field, not just touch,
     * since unison_extended_input bundles three independent input kinds
     * that a caller could easily wire up in the wrong order. */
    uint8_t buf[UNISON_EXTENDED_INPUT_FRAME_SIZE];
    unison_extended_input input;
    input.pressed = 1;
    input.touch_x = 160;
    input.touch_y = 120;
    input.buttons = UNISON_BUTTON_A | UNISON_BUTTON_HOME;
    input.left_x = -32768;
    input.left_y = 32767;
    input.right_x = 0;
    input.right_y = -1;

    CHECK(unison_build_extended_input_frame(&input, buf) == UNISON_EXTENDED_INPUT_FRAME_SIZE);
    CHECK(buf[0] == UNISON_MSG_INPUT);

    unison_extended_input parsed;
    CHECK(unison_parse_extended_input_frame(buf, sizeof(buf), &parsed) == UNISON_OK);
    CHECK(parsed.pressed != 0);
    CHECK(parsed.touch_x == 160);
    CHECK(parsed.touch_y == 120);
    CHECK(parsed.buttons == (UNISON_BUTTON_A | UNISON_BUTTON_HOME));
    CHECK(parsed.left_x == -32768);
    CHECK(parsed.left_y == 32767);
    CHECK(parsed.right_x == 0);
    CHECK(parsed.right_y == -1);

    /* Too short to even hold the fixed frame */
    CHECK(unison_parse_extended_input_frame(buf, UNISON_EXTENDED_INPUT_FRAME_SIZE - 1, &parsed) ==
          UNISON_ERR_TOO_SHORT);
}

static void test_extended_input_frame_released(void) {
    /* Same "release carries no position" convention as unison_touch_state
     * (test_touch_frame_released) -- buttons/sticks are independent of
     * touch and must NOT be zeroed just because pressed is 0. */
    uint8_t buf[UNISON_EXTENDED_INPUT_FRAME_SIZE];
    unison_extended_input input;
    input.pressed = 0;
    input.touch_x = 999;
    input.touch_y = 999;
    input.buttons = UNISON_BUTTON_START;
    input.left_x = 100;
    input.left_y = -100;
    input.right_x = 50;
    input.right_y = -50;

    CHECK(unison_build_extended_input_frame(&input, buf) == UNISON_EXTENDED_INPUT_FRAME_SIZE);

    unison_extended_input parsed;
    CHECK(unison_parse_extended_input_frame(buf, sizeof(buf), &parsed) == UNISON_OK);
    CHECK(parsed.pressed == 0);
    CHECK(parsed.touch_x == 0);
    CHECK(parsed.touch_y == 0);
    CHECK(parsed.buttons == UNISON_BUTTON_START);
    CHECK(parsed.left_x == 100);
    CHECK(parsed.left_y == -100);
    CHECK(parsed.right_x == 50);
    CHECK(parsed.right_y == -50);
}

static void test_touch_and_buttons_frame(void) {
    /* NDS_BOTTOM_SCREEN's input_encoding -- same idea as extended_input but
     * without the two always-zero stick fields (the DS has no analog
     * stick), a genuinely smaller wire shape rather than padding. */
    uint8_t buf[UNISON_TOUCH_AND_BUTTONS_FRAME_SIZE];
    unison_touch_and_buttons input;
    input.pressed = 1;
    input.touch_x = 128;
    input.touch_y = 96;
    input.buttons = UNISON_BUTTON_A | UNISON_BUTTON_B | UNISON_BUTTON_UP;

    CHECK(unison_build_touch_and_buttons_frame(&input, buf) == UNISON_TOUCH_AND_BUTTONS_FRAME_SIZE);
    CHECK(buf[0] == UNISON_MSG_INPUT);

    unison_touch_and_buttons parsed;
    CHECK(unison_parse_touch_and_buttons_frame(buf, sizeof(buf), &parsed) == UNISON_OK);
    CHECK(parsed.pressed != 0);
    CHECK(parsed.touch_x == 128);
    CHECK(parsed.touch_y == 96);
    CHECK(parsed.buttons == (UNISON_BUTTON_A | UNISON_BUTTON_B | UNISON_BUTTON_UP));

    /* Release: touch_x/touch_y forced to 0, buttons independent (same
     * convention as extended_input above) */
    input.pressed = 0;
    input.touch_x = 999;
    input.touch_y = 999;
    input.buttons = UNISON_BUTTON_SELECT;
    CHECK(unison_build_touch_and_buttons_frame(&input, buf) == UNISON_TOUCH_AND_BUTTONS_FRAME_SIZE);
    CHECK(unison_parse_touch_and_buttons_frame(buf, sizeof(buf), &parsed) == UNISON_OK);
    CHECK(parsed.pressed == 0);
    CHECK(parsed.touch_x == 0);
    CHECK(parsed.touch_y == 0);
    CHECK(parsed.buttons == UNISON_BUTTON_SELECT);

    CHECK(unison_parse_touch_and_buttons_frame(buf, UNISON_TOUCH_AND_BUTTONS_FRAME_SIZE - 1, &parsed) ==
          UNISON_ERR_TOO_SHORT);
}

static void test_text_input_request(void) {
    /* Server -> client swkbd prompt, incl. pre-filled text -- confirm the
     * variable-length text round-trips byte-exact, not just the fixed
     * header (max_length/text_len are easy to get right while still
     * mis-copying the text itself). */
    const char *text = "Hello, world!";
    unison_text_input_request req;
    req.max_length = 64;
    req.text = text;
    req.text_len = strlen(text);

    size_t needed = unison_text_input_request_max_size(req.text_len);
    uint8_t *buf = malloc(needed);
    CHECK(buf != NULL);

    size_t written = unison_build_text_input_request(&req, buf, needed);
    CHECK(written == UNISON_TEXT_INPUT_REQUEST_HEADER_SIZE + req.text_len);
    CHECK(buf[0] == UNISON_MSG_TEXT_INPUT_REQUEST);

    unison_text_input_request parsed;
    CHECK(unison_parse_text_input_request(buf, written, &parsed) == UNISON_OK);
    CHECK(parsed.max_length == 64);
    CHECK(parsed.text_len == req.text_len);
    CHECK(memcmp(parsed.text, text, req.text_len) == 0);

    /* out_capacity too small -> 0, not a truncated/corrupt write */
    CHECK(unison_build_text_input_request(&req, buf, needed - 1) == 0);

    free(buf);

    /* No pre-filled text at all (text_len == 0) must still round-trip
     * cleanly, not be confused with a parse failure. */
    unison_text_input_request empty_req;
    empty_req.max_length = 0;
    empty_req.text = "";
    empty_req.text_len = 0;
    uint8_t small_buf[UNISON_TEXT_INPUT_REQUEST_HEADER_SIZE];
    size_t empty_written = unison_build_text_input_request(&empty_req, small_buf, sizeof(small_buf));
    CHECK(empty_written == UNISON_TEXT_INPUT_REQUEST_HEADER_SIZE);
    unison_text_input_request empty_parsed;
    CHECK(unison_parse_text_input_request(small_buf, empty_written, &empty_parsed) == UNISON_OK);
    CHECK(empty_parsed.max_length == 0);
    CHECK(empty_parsed.text_len == 0);
}

static void test_text_input_response(void) {
    /* Client -> server typed result, confirmed case */
    const char *text = "Player One";
    unison_text_input_response resp;
    resp.confirmed = 1;
    resp.text = text;
    resp.text_len = strlen(text);

    size_t needed = unison_text_input_response_max_size(resp.text_len);
    uint8_t *buf = malloc(needed);
    CHECK(buf != NULL);

    size_t written = unison_build_text_input_response(&resp, buf, needed);
    CHECK(written == UNISON_TEXT_INPUT_RESPONSE_HEADER_SIZE + resp.text_len);
    CHECK(buf[0] == UNISON_MSG_TEXT_INPUT_RESPONSE);

    unison_text_input_response parsed;
    CHECK(unison_parse_text_input_response(buf, written, &parsed) == UNISON_OK);
    CHECK(parsed.confirmed != 0);
    CHECK(parsed.text_len == resp.text_len);
    CHECK(memcmp(parsed.text, text, resp.text_len) == 0);

    free(buf);

    /* Cancelled: per unison_text_input_response's own comment, text/
     * text_len are meaningless here -- only confirmed==0 itself matters to
     * a correct caller, but the frame must still parse cleanly either way
     * (a malformed cancel frame shouldn't be indistinguishable from a
     * genuine parse error). */
    unison_text_input_response cancel;
    cancel.confirmed = 0;
    cancel.text = "ignored";
    cancel.text_len = 7;
    size_t cancel_needed = unison_text_input_response_max_size(cancel.text_len);
    uint8_t *cancel_buf = malloc(cancel_needed);
    CHECK(cancel_buf != NULL);
    size_t cancel_written = unison_build_text_input_response(&cancel, cancel_buf, cancel_needed);
    unison_text_input_response cancel_parsed;
    CHECK(unison_parse_text_input_response(cancel_buf, cancel_written, &cancel_parsed) == UNISON_OK);
    CHECK(cancel_parsed.confirmed == 0);
    free(cancel_buf);
}

static void test_mic_enable_frame(void) {
    /* Server -> client mic level signal -- sample_rate only meaningful
     * when enabled=1, but must still round-trip whatever value was sent
     * when enabled=0 (a client that reads it anyway despite the "meaningless"
     * doc note shouldn't see corrupted data, just possibly-stale data). */
    uint8_t buf[UNISON_MIC_ENABLE_FRAME_SIZE];
    unison_mic_enable enable;
    enable.enabled = 1;
    enable.sample_rate = 16000;

    CHECK(unison_build_mic_enable_frame(&enable, buf) == UNISON_MIC_ENABLE_FRAME_SIZE);
    CHECK(buf[0] == UNISON_MSG_MIC_ENABLE);

    unison_mic_enable parsed;
    CHECK(unison_parse_mic_enable_frame(buf, sizeof(buf), &parsed) == UNISON_OK);
    CHECK(parsed.enabled != 0);
    CHECK(parsed.sample_rate == 16000);

    enable.enabled = 0;
    enable.sample_rate = 0;
    CHECK(unison_build_mic_enable_frame(&enable, buf) == UNISON_MIC_ENABLE_FRAME_SIZE);
    CHECK(unison_parse_mic_enable_frame(buf, sizeof(buf), &parsed) == UNISON_OK);
    CHECK(parsed.enabled == 0);

    CHECK(unison_parse_mic_enable_frame(buf, UNISON_MIC_ENABLE_FRAME_SIZE - 1, &parsed) ==
          UNISON_ERR_TOO_SHORT);
}

static void test_mic_audio_frame(void) {
    /* Client -> server mic PCM, type=7 -- identical wire shape to
     * unison_audio_frame (type=3, see unison_parse_mic_audio_frame's own
     * comment), hand-built here since there's no build helper (only the
     * Android client currently produces this message, and it hand-builds
     * it too). Mono per the doc comment: channels is still an explicit
     * wire field, not implicitly always 1, so confirm it parses back
     * exactly what was sent rather than being silently forced to 1. */
    const uint8_t frame[] = {
        7,
        0x80, 0x3E, 0x00, 0x00, /* 16000 LE */
        1,                       /* channels = 1 (mono) */
        0x2C, 0x01,              /* sample 0 = 300 */
        0xD4, 0xFE               /* sample 1 = -300 */
    };

    unison_audio_frame audio;
    CHECK(unison_parse_mic_audio_frame(frame, sizeof(frame), &audio) == UNISON_OK);
    CHECK(audio.sample_rate == 16000);
    CHECK(audio.channels == 1);
    CHECK(audio.sample_count == 2);
    CHECK(unison_read_s16le(audio.samples) == 300);
    CHECK(unison_read_s16le(audio.samples + 2) == -300);

    /* Wrong leading type byte (type=3, not type=7) must be rejected -- mic
     * audio and console audio share a wire shape but are NOT interchangeable
     * messages (see unison_msg_type; a receiver must be able to tell them
     * apart by the type byte alone). */
    uint8_t wrong_type[sizeof(frame)];
    memcpy(wrong_type, frame, sizeof(frame));
    wrong_type[0] = 3;
    CHECK(unison_parse_mic_audio_frame(wrong_type, sizeof(wrong_type), &audio) == UNISON_ERR_UNKNOWN_TYPE);
}

int main(void) {
    test_peek_type();
    test_video_header();
    test_max_inflated_size();
    test_decode_video_frame_full_raw();
    test_decode_video_frame_full_indexed();
    test_decode_video_frame_tiles();
    test_decode_video_frame_tiles_indexed();
    test_audio_frame();
    test_build_input_frame();
    test_touch_frame_pressed();
    test_touch_frame_released();
    test_extended_input_frame_pressed();
    test_extended_input_frame_released();
    test_touch_and_buttons_frame();
    test_text_input_request();
    test_text_input_response();
    test_mic_enable_frame();
    test_mic_audio_frame();
    printf("protocol: all tests passed\n");
    return 0;
}
