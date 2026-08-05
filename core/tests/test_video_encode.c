#include "unison/deflate.h"
#include "unison/inflate.h"
#include "unison/protocol.h"
#include "unison/video_encode.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            exit(1);                                                            \
        }                                                                       \
    } while (0)

/* Encodes current against previous (may be NULL), inflates+decodes the
 * result onto a copy of previous (or a freshly zeroed buffer, matching
 * what a real client's persistent framebuffer would already hold before
 * this call), and checks it now matches current exactly -- the same
 * "round-trip through the real decoder" approach test_deflate.c uses for
 * unison_deflate_raw(). Returns the unison_encode_status so callers can
 * also assert on UNISON_ENCODE_UNCHANGED. */
static unison_encode_status encode_decode_and_check(const uint8_t *current, const uint8_t *previous,
                                                       uint32_t width, uint32_t height) {
    size_t pixel_bytes = (size_t)width * height * 2;

    size_t scratch_cap = unison_video_max_inflated_size(width, height);
    uint8_t *scratch = malloc(scratch_cap);
    CHECK(scratch != NULL);

    size_t out_cap = unison_video_encode_max_size(width, height);
    uint8_t *out = malloc(out_cap);
    CHECK(out != NULL);

    size_t out_size = 0;
    uint8_t format = 0xFF;
    unison_encode_status status = unison_encode_video_frame(current, previous, width, height, scratch,
                                                                scratch_cap, out, out_cap, &out_size, &format);

    if (status == UNISON_ENCODE_OK) {
        uint8_t *inflated = malloc(scratch_cap);
        CHECK(inflated != NULL);
        size_t inflated_size = 0;
        CHECK(unison_inflate_raw(out, out_size, inflated, scratch_cap, &inflated_size) == UNISON_INFLATE_OK);

        uint8_t *framebuffer = malloc(pixel_bytes);
        CHECK(framebuffer != NULL);
        if (previous != NULL) {
            memcpy(framebuffer, previous, pixel_bytes); /* TILES only patches the changed part */
        } else {
            memset(framebuffer, 0xAA, pixel_bytes); /* full frame must overwrite all of this */
        }

        CHECK(unison_decode_video_frame(format, inflated, inflated_size, width, height, framebuffer,
                                          pixel_bytes) == UNISON_OK);
        CHECK(memcmp(framebuffer, current, pixel_bytes) == 0);

        free(framebuffer);
        free(inflated);
    }

    free(out);
    free(scratch);
    return status;
}

static void fill_pattern(uint8_t *buf, uint32_t width, uint32_t height, uint16_t seed) {
    for (size_t i = 0; i < (size_t)width * height; i++) {
        uint16_t pixel = (uint16_t)(seed + i * 7);
        buf[i * 2 + 0] = (uint8_t)(pixel & 0xFF);
        buf[i * 2 + 1] = (uint8_t)(pixel >> 8);
    }
}

int main(void) {
    /* Deliberately not a multiple of 8 in either dimension, matching
     * WIIU_GAMEPAD's real 854x480 (854 = 106*8 + 6) -- exercises the
     * edge-tile padding path in write_tile_pixels(). Kept small for a fast
     * test; the edge-tile logic doesn't depend on the actual resolution. */
    const uint32_t width = 20;
    const uint32_t height = 12;
    size_t pixel_bytes = (size_t)width * height * 2;

    uint8_t *frame_a = malloc(pixel_bytes);
    uint8_t *frame_b = malloc(pixel_bytes);
    CHECK(frame_a != NULL && frame_b != NULL);
    fill_pattern(frame_a, width, height, 0x1234);
    memcpy(frame_b, frame_a, pixel_bytes);

    /* First frame of a session (previous = NULL): always a full keyframe,
     * format 0. */
    CHECK(encode_decode_and_check(frame_a, NULL, width, height) == UNISON_ENCODE_OK);

    /* Pixel-identical to the previous frame: dedup, nothing to send. */
    CHECK(encode_decode_and_check(frame_b, frame_a, width, height) == UNISON_ENCODE_UNCHANGED);

    /* Change exactly one pixel inside one tile (tile (1,1), covering
     * pixels x=8..15/y=8..11 given height=12 makes that tile's rows 8-11
     * a partial edge tile too) -- only that tile should differ, and
     * decode must leave every other pixel exactly as it was in `previous`. */
    memcpy(frame_b, frame_a, pixel_bytes);
    size_t changed_pixel = (size_t)9 * width + 10; /* inside tile (1,1) */
    frame_b[changed_pixel * 2 + 0] ^= 0xFF;
    frame_b[changed_pixel * 2 + 1] ^= 0xFF;
    CHECK(encode_decode_and_check(frame_b, frame_a, width, height) == UNISON_ENCODE_OK);

    /* Change pixels in two tiles, including the partial edge tile at the
     * right edge (width=20 -> tiles_per_row=3, last tile only 4px wide). */
    memcpy(frame_b, frame_a, pixel_bytes);
    frame_b[(0 * width + 0) * 2] ^= 0xFF;   /* tile (0,0) */
    frame_b[(2 * width + 18) * 2] ^= 0xFF;  /* tile (2,0), x=18 is in the partial last column */
    CHECK(encode_decode_and_check(frame_b, frame_a, width, height) == UNISON_ENCODE_OK);

    /* A completely different frame (every tile changed) still round-trips
     * correctly, not just the sparse-diff cases above. */
    fill_pattern(frame_b, width, height, 0xBEEF);
    CHECK(encode_decode_and_check(frame_b, frame_a, width, height) == UNISON_ENCODE_OK);

    free(frame_a);
    free(frame_b);

    printf("video_encode: all tests passed\n");
    return 0;
}
