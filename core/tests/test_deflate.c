#include "unison/deflate.h"
#include "unison/inflate.h"

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

int main(void) {
    /* Round-trip through unison_inflate_raw() (the only thing that
     * verified raw-deflate-ness before this file existed, see
     * test_inflate.c's own comment) rather than hand-building a known
     * compressed stream -- this is the more meaningful test for an
     * encoder: "does what it produces actually decode back to the
     * original", not "does it produce these exact bytes". */
    uint8_t src[4096];
    for (size_t i = 0; i < sizeof(src); i++) {
        /* Not all-zero/all-same-byte: a real compressor could special-case
         * that trivially, and it wouldn't exercise much. Simple repeating
         * pattern with some variation instead. */
        src[i] = (uint8_t)((i * 7 + i / 13) & 0xFF);
    }

    size_t max_size = unison_deflate_max_size(sizeof(src));
    CHECK(max_size >= sizeof(src)); /* worst case must never be smaller than the input */

    uint8_t *compressed = malloc(max_size);
    CHECK(compressed != NULL);
    size_t compressed_size = 0;
    unison_deflate_status dstatus =
        unison_deflate_raw(src, sizeof(src), compressed, max_size, &compressed_size);
    CHECK(dstatus == UNISON_DEFLATE_OK);
    CHECK(compressed_size > 0);
    CHECK(compressed_size <= max_size);

    uint8_t decoded[sizeof(src)];
    size_t decoded_size = 0;
    unison_inflate_status istatus =
        unison_inflate_raw(compressed, compressed_size, decoded, sizeof(decoded), &decoded_size);
    CHECK(istatus == UNISON_INFLATE_OK);
    CHECK(decoded_size == sizeof(src));
    CHECK(memcmp(decoded, src, sizeof(src)) == 0);

    free(compressed);

    /* Output buffer too small must fail cleanly, not overflow. */
    uint8_t tiny_out[4];
    size_t tiny_out_size = 0;
    dstatus = unison_deflate_raw(src, sizeof(src), tiny_out, sizeof(tiny_out), &tiny_out_size);
    CHECK(dstatus == UNISON_DEFLATE_ERR);

    printf("deflate: all tests passed\n");
    return 0;
}
