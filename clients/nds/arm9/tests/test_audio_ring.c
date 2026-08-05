/* Host-buildable (plain gcc/clang, no devkitARM) unit test for
 * unison_nds_audio_ring_* (audio_ring.h/.c) -- the dual-audio-client test
 * category's "Audiosignal wird empfangen und wiedergegeben" check for the
 * NDS client's GC_GBA_LINK audio path. Links audio_ring.c + unison_core
 * directly (for unison_read_s16le), same pattern as
 * test_dual_screen_choice.c. */

#include "audio_ring.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unison/endian.h"

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            exit(1);                                                            \
        }                                                                         \
    } while (0)

static void encode_s16le(int16_t value, uint8_t *out) {
    unison_write_u16le(out, (uint16_t)value);
}

static void test_push_then_pull_fifo_order(void) {
    int16_t backing[8];
    unison_nds_audio_ring ring;
    unison_nds_audio_ring_init(&ring, backing, 8);

    uint8_t wire[3 * 2];
    encode_s16le(100, wire + 0);
    encode_s16le(-200, wire + 2);
    encode_s16le(300, wire + 4);
    unison_nds_audio_ring_push(&ring, wire, 3);

    int16_t out[3];
    unison_nds_audio_ring_pull(&ring, out, 3);
    CHECK(out[0] == 100);
    CHECK(out[1] == -200);
    CHECK(out[2] == 300);
}

static void test_underrun_pads_with_silence(void) {
    /* Fewer samples buffered than requested -- must pad the REMAINDER with
     * 0, not replay stale samples or leave them uninitialized (this is the
     * exact "network hasn't delivered enough yet" case). */
    int16_t backing[8];
    unison_nds_audio_ring ring;
    unison_nds_audio_ring_init(&ring, backing, 8);

    uint8_t wire[2 * 2];
    encode_s16le(111, wire + 0);
    encode_s16le(222, wire + 2);
    unison_nds_audio_ring_push(&ring, wire, 2);

    int16_t out[5];
    memset(out, 0xAB, sizeof(out)); /* poison, so a missed silence-fill would be caught */
    unison_nds_audio_ring_pull(&ring, out, 5);
    CHECK(out[0] == 111);
    CHECK(out[1] == 222);
    CHECK(out[2] == 0);
    CHECK(out[3] == 0);
    CHECK(out[4] == 0);
}

static void test_overflow_drops_tail_not_existing_samples(void) {
    /* Ring capacity 4, already holding 3 unread samples -- pushing 5 more
     * must keep the original 3 intact and only append 1 more (dropping the
     * other 4), never overwrite not-yet-played samples. */
    int16_t backing[4];
    unison_nds_audio_ring ring;
    unison_nds_audio_ring_init(&ring, backing, 4);

    uint8_t first[3 * 2];
    encode_s16le(1, first + 0);
    encode_s16le(2, first + 2);
    encode_s16le(3, first + 4);
    unison_nds_audio_ring_push(&ring, first, 3);

    uint8_t second[5 * 2];
    for (int i = 0; i < 5; i++) {
        encode_s16le((int16_t)(100 + i), second + i * 2);
    }
    unison_nds_audio_ring_push(&ring, second, 5);

    int16_t out[4];
    unison_nds_audio_ring_pull(&ring, out, 4);
    CHECK(out[0] == 1);
    CHECK(out[1] == 2);
    CHECK(out[2] == 3);
    CHECK(out[3] == 100); /* only the first of the 5 fit before the ring was full */

    /* Ring now empty -- confirm no leftover/garbage samples remain queued. */
    int16_t drained[2];
    memset(drained, 0xCD, sizeof(drained));
    unison_nds_audio_ring_pull(&ring, drained, 2);
    CHECK(drained[0] == 0);
    CHECK(drained[1] == 0);
}

static void test_wraparound(void) {
    /* Push/pull repeatedly past the physical end of the backing array to
     * exercise the modulo wraparound in both write_pos and read_pos. */
    int16_t backing[4];
    unison_nds_audio_ring ring;
    unison_nds_audio_ring_init(&ring, backing, 4);

    for (int round = 0; round < 3; round++) {
        uint8_t wire[3 * 2];
        int16_t base = (int16_t)(round * 10);
        encode_s16le(base, wire + 0);
        encode_s16le((int16_t)(base + 1), wire + 2);
        encode_s16le((int16_t)(base + 2), wire + 4);
        unison_nds_audio_ring_push(&ring, wire, 3);

        int16_t out[3];
        unison_nds_audio_ring_pull(&ring, out, 3);
        CHECK(out[0] == base);
        CHECK(out[1] == base + 1);
        CHECK(out[2] == base + 2);
    }
}

static void test_reset_drops_buffered_audio(void) {
    int16_t backing[4];
    unison_nds_audio_ring ring;
    unison_nds_audio_ring_init(&ring, backing, 4);

    uint8_t wire[2 * 2];
    encode_s16le(42, wire + 0);
    encode_s16le(43, wire + 2);
    unison_nds_audio_ring_push(&ring, wire, 2);

    unison_nds_audio_ring_reset(&ring);

    int16_t out[2];
    memset(out, 0xEF, sizeof(out));
    unison_nds_audio_ring_pull(&ring, out, 2);
    CHECK(out[0] == 0);
    CHECK(out[1] == 0);
}

int main(void) {
    test_push_then_pull_fifo_order();
    test_underrun_pads_with_silence();
    test_overflow_drops_tail_not_existing_samples();
    test_wraparound();
    test_reset_drops_buffered_audio();
    printf("audio_ring (nds): all tests passed\n");
    return 0;
}
