#include "unison/inflate.h"

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

int main(void) {
    /* Minimal raw-deflate stream: one final "stored" (uncompressed) block
     * wrapping the 2 bytes "AB". Hand-built so this test needs no deflate
     * encoder: BFINAL=1, BTYPE=00, byte-aligned, LEN=0x0002, NLEN=~LEN,
     * then the raw payload. */
    const uint8_t compressed[] = {
        0x01,             /* BFINAL=1, BTYPE=00 (stored), rest padding */
        0x02, 0x00,       /* LEN = 2 */
        0xFD, 0xFF,       /* NLEN = ~LEN */
        'A', 'B'
    };

    uint8_t out[8];
    size_t out_size = 0;
    unison_inflate_status status =
        unison_inflate_raw(compressed, sizeof(compressed), out, sizeof(out), &out_size);

    CHECK(status == UNISON_INFLATE_OK);
    CHECK(out_size == 2);
    CHECK(memcmp(out, "AB", 2) == 0);

    /* Output buffer too small for the decompressed size must fail cleanly,
     * not overflow. */
    uint8_t tiny_out[1];
    size_t tiny_out_size = 0;
    status = unison_inflate_raw(compressed, sizeof(compressed), tiny_out, sizeof(tiny_out), &tiny_out_size);
    CHECK(status == UNISON_INFLATE_ERR);

    printf("inflate: all tests passed\n");
    return 0;
}
