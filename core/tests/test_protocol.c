#include "finlink/protocol.h"
#include "finlink/endian.h"

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
    finlink_msg_type type;

    CHECK(finlink_peek_type(NULL, 0, &type) == FINLINK_ERR_TOO_SHORT);

    const uint8_t video_byte[] = {1};
    CHECK(finlink_peek_type(video_byte, sizeof(video_byte), &type) == FINLINK_OK);
    CHECK(type == FINLINK_MSG_VIDEO);

    const uint8_t bogus_byte[] = {42};
    CHECK(finlink_peek_type(bogus_byte, sizeof(bogus_byte), &type) == FINLINK_ERR_UNKNOWN_TYPE);
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

    finlink_video_header hdr;
    CHECK(finlink_parse_video_header(frame, sizeof(frame), &hdr) == FINLINK_OK);
    CHECK(hdr.width == 240);
    CHECK(hdr.height == 160);
    CHECK(hdr.format == 0);
    CHECK(hdr.compressed_size == 2);
    CHECK(hdr.compressed_data[0] == 0xAA);
    CHECK(hdr.compressed_data[1] == 0xBB);

    CHECK(finlink_parse_video_header(frame, 9, &hdr) == FINLINK_ERR_TOO_SHORT);

    /* format is parsed as a plain byte -- finlink_decode_video_frame() is
     * what interprets the bits */
    const uint8_t indexed_frame[] = {
        1,
        4, 0, 0, 0,
        1, 0, 0, 0,
        FINLINK_VIDEO_FORMAT_INDEXED | FINLINK_VIDEO_FORMAT_TILES,
        0xCC
    };
    CHECK(finlink_parse_video_header(indexed_frame, sizeof(indexed_frame), &hdr) == FINLINK_OK);
    CHECK(hdr.format == (FINLINK_VIDEO_FORMAT_INDEXED | FINLINK_VIDEO_FORMAT_TILES));
    CHECK(hdr.compressed_size == 1);
}

static void test_max_inflated_size(void) {
    /* Always at least enough for a full non-indexed frame, the cheapest
     * upper bound any format could possibly need. */
    CHECK(finlink_video_max_inflated_size(240, 160) > (size_t)240 * 160 * 2);
    /* Non-multiple-of-8 dimensions round the tile grid up, not down. */
    CHECK(finlink_video_max_inflated_size(9, 9) >= (size_t)9 * 9 * 2 + 4 + 4 * 2);
}

static void test_decode_video_frame_full_raw(void) {
    /* 2x1 raw RGB565: pixel 0 = 0x1234, pixel 1 = 0x5678 (both LE) */
    const uint8_t inflated[] = { 0x34, 0x12, 0x78, 0x56 };
    uint8_t out[4];

    CHECK(finlink_decode_video_frame(0, inflated, sizeof(inflated), 2, 1, out, sizeof(out)) == FINLINK_OK);
    CHECK(finlink_read_u16le(out) == 0x1234);
    CHECK(finlink_read_u16le(out + 2) == 0x5678);

    /* Not enough inflated data for the promised width*height */
    CHECK(finlink_decode_video_frame(0, inflated, 2, 2, 1, out, sizeof(out)) == FINLINK_ERR_TOO_SHORT);

    /* Not enough room in the caller's framebuffer */
    CHECK(finlink_decode_video_frame(0, inflated, sizeof(inflated), 2, 1, out, 3) == FINLINK_ERR_TOO_SHORT);
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

    CHECK(finlink_decode_video_frame(FINLINK_VIDEO_FORMAT_INDEXED, inflated, sizeof(inflated), 2, 2, out,
                                      sizeof(out)) == FINLINK_OK);
    CHECK(finlink_read_u16le(out + 0) == 0x1111);
    CHECK(finlink_read_u16le(out + 2) == 0x2222);
    CHECK(finlink_read_u16le(out + 4) == 0x2222);
    CHECK(finlink_read_u16le(out + 6) == 0x1111);

    /* Index pointing outside the palette it came with */
    const uint8_t bad_index[] = { 1, 0, 0x11, 0x11, 5 };
    uint8_t small_out[2];
    CHECK(finlink_decode_video_frame(FINLINK_VIDEO_FORMAT_INDEXED, bad_index, sizeof(bad_index), 1, 1, small_out,
                                      sizeof(small_out)) == FINLINK_ERR_TOO_SHORT);

    /* Unknown bit set (only bits 0 and 1 are defined) */
    CHECK(finlink_decode_video_frame(4, inflated, sizeof(inflated), 2, 2, out, sizeof(out)) ==
          FINLINK_ERR_UNKNOWN_FORMAT);
}

static void fill_u16le(uint8_t *buf, size_t count, uint16_t value) {
    for (size_t i = 0; i < count; i++) {
        finlink_write_u16le(buf + i * 2, value);
    }
}

static void test_decode_video_frame_tiles(void) {
    /* 16x8 framebuffer = 2x1 tiles (tile 0 = left half, tile 1 = right
     * half). Paint a full raw frame first, then a TILES-only patch that
     * only touches tile 1, and confirm tile 0's pixels are untouched --
     * this is the whole point of TILES: framebuffer_rgb565 is persistent
     * across calls, not reset by finlink_decode_video_frame() itself. */
    const uint32_t width = 16, height = 8;
    uint8_t framebuffer[16 * 8 * 2];

    uint8_t full_frame[16 * 8 * 2];
    fill_u16le(full_frame, 16 * 8, 0xAAAA);
    CHECK(finlink_decode_video_frame(0, full_frame, sizeof(full_frame), width, height, framebuffer,
                                      sizeof(framebuffer)) == FINLINK_OK);
    for (size_t i = 0; i < 16 * 8; i++) {
        CHECK(finlink_read_u16le(framebuffer + i * 2) == 0xAAAA);
    }

    uint8_t patch[2 + 1 * 2 + 64 * 2];
    finlink_write_u16le(patch, 1);      /* tile_count = 1 */
    finlink_write_u16le(patch + 2, 1);  /* tile_index = 1 (the right tile) */
    fill_u16le(patch + 4, 64, 0xBBBB);
    CHECK(finlink_decode_video_frame(FINLINK_VIDEO_FORMAT_TILES, patch, sizeof(patch), width, height, framebuffer,
                                      sizeof(framebuffer)) == FINLINK_OK);

    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            uint16_t expected = x < 8 ? 0xAAAA : 0xBBBB;
            CHECK(finlink_read_u16le(framebuffer + (y * width + x) * 2) == expected);
        }
    }

    /* Corrupt tile index (only 2 tiles exist, 0-1) */
    uint8_t bad_patch[2 + 1 * 2];
    finlink_write_u16le(bad_patch, 1);
    finlink_write_u16le(bad_patch + 2, 99);
    CHECK(finlink_decode_video_frame(FINLINK_VIDEO_FORMAT_TILES, bad_patch, sizeof(bad_patch), width, height,
                                      framebuffer, sizeof(framebuffer)) == FINLINK_ERR_TOO_SHORT);
}

static void test_decode_video_frame_tiles_indexed(void) {
    /* Same 16x8 layout, but the tile patch is palette-indexed: 1-color
     * palette (0xCCCC), all 64 pixels use index 0. */
    const uint32_t width = 16, height = 8;
    uint8_t framebuffer[16 * 8 * 2];
    fill_u16le(framebuffer, 16 * 8, 0x0000);

    uint8_t patch[2 + 1 * 2 + 2 + 1 * 2 + 64];
    size_t off = 0;
    finlink_write_u16le(patch + off, 1);
    off += 2; /* tile_count */
    finlink_write_u16le(patch + off, 0);
    off += 2; /* tile_index = 0 (left tile) */
    finlink_write_u16le(patch + off, 1);
    off += 2; /* palette_count */
    finlink_write_u16le(patch + off, 0xCCCC);
    off += 2; /* palette[0] */
    memset(patch + off, 0, 64); /* every pixel uses palette index 0 */

    uint8_t format = FINLINK_VIDEO_FORMAT_TILES | FINLINK_VIDEO_FORMAT_INDEXED;
    CHECK(finlink_decode_video_frame(format, patch, sizeof(patch), width, height, framebuffer,
                                      sizeof(framebuffer)) == FINLINK_OK);

    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < width; x++) {
            uint16_t expected = x < 8 ? 0xCCCC : 0x0000;
            CHECK(finlink_read_u16le(framebuffer + (y * width + x) * 2) == expected);
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

    finlink_audio_frame audio;
    CHECK(finlink_parse_audio_frame(frame, sizeof(frame), &audio) == FINLINK_OK);
    CHECK(audio.sample_rate == 32000);
    CHECK(audio.channels == 2);
    CHECK(audio.sample_count == 2);
    CHECK(finlink_read_s16le(audio.samples) == -1);
    CHECK(finlink_read_s16le(audio.samples + 2) == 1000);
}

static void test_build_input_frame(void) {
    uint8_t buf[FINLINK_INPUT_FRAME_SIZE];
    uint16_t mask = FINLINK_KEY_A | FINLINK_KEY_START;

    CHECK(finlink_build_input_frame(mask, buf) == FINLINK_INPUT_FRAME_SIZE);
    CHECK(buf[0] == FINLINK_MSG_INPUT);
    CHECK(finlink_read_u16le(buf + 1) == mask);
}

static void test_touch_frame_pressed(void) {
    uint8_t buf[FINLINK_TOUCH_FRAME_SIZE];
    finlink_touch_state touch;
    touch.pressed = 1;
    touch.x = 160;
    touch.y = 120;

    CHECK(finlink_build_touch_frame(&touch, buf) == FINLINK_TOUCH_FRAME_SIZE);
    CHECK(buf[0] == FINLINK_MSG_INPUT);
    CHECK(buf[1] == 1);
    CHECK(finlink_read_u16le(buf + 2) == 160);
    CHECK(finlink_read_u16le(buf + 4) == 120);

    finlink_touch_state parsed;
    CHECK(finlink_parse_touch_frame(buf, sizeof(buf), &parsed) == FINLINK_OK);
    CHECK(parsed.pressed != 0);
    CHECK(parsed.x == 160);
    CHECK(parsed.y == 120);
}

static void test_touch_frame_released(void) {
    /* A release must always encode x/y as 0 regardless of what the caller
     * passes in, per finlink_touch_state's own comment -- a release has no
     * meaningful position. */
    uint8_t buf[FINLINK_TOUCH_FRAME_SIZE];
    finlink_touch_state touch;
    touch.pressed = 0;
    touch.x = 999;
    touch.y = 999;

    CHECK(finlink_build_touch_frame(&touch, buf) == FINLINK_TOUCH_FRAME_SIZE);
    CHECK(buf[1] == 0);
    CHECK(finlink_read_u16le(buf + 2) == 0);
    CHECK(finlink_read_u16le(buf + 4) == 0);

    finlink_touch_state parsed;
    CHECK(finlink_parse_touch_frame(buf, sizeof(buf), &parsed) == FINLINK_OK);
    CHECK(parsed.pressed == 0);
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
    printf("protocol: all tests passed\n");
    return 0;
}
