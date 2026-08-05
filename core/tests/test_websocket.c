#include "unison/websocket.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                          \
        }                                                                     \
    } while (0)

static void test_generate_key(void) {
    uint8_t zeros[16] = {0};
    char key[UNISON_WS_KEY_BUF_LEN];

    unison_ws_generate_key(zeros, key);
    CHECK(strlen(key) == UNISON_WS_KEY_LEN);
    CHECK(strcmp(key, "AAAAAAAAAAAAAAAAAAAAAA==") == 0);
}

static void test_build_handshake_request(void) {
    char buf[256];
    /* RFC6455 sec 1.2 example key */
    const char *key = "dGhlIHNhbXBsZSBub25jZQ==";

    size_t n = unison_ws_build_handshake_request("localhost:6801", "/", key, buf, sizeof(buf));
    CHECK(n > 0);
    buf[n] = '\0';
    CHECK(strstr(buf, "GET / HTTP/1.1\r\n") == buf);
    CHECK(strstr(buf, "Host: localhost:6801\r\n") != NULL);
    CHECK(strstr(buf, "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n") != NULL);
    CHECK(strstr(buf, "\r\n\r\n") != NULL);

    /* Buffer too small must fail cleanly, not truncate silently. */
    char tiny[8];
    CHECK(unison_ws_build_handshake_request("localhost:6801", "/", key, tiny, sizeof(tiny)) == 0);
}

static void test_parse_handshake_response(void) {
    /* RFC6455 sec 1.3 worked example: this key/accept pair is the spec's own
     * test vector, so this also incidentally exercises our SHA1+base64. */
    const char *key = "dGhlIHNhbXBsZSBub25jZQ==";
    const char *response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
        "\r\n"
        "\x01\x02trailing-frame-bytes";

    size_t header_len = 0;
    unison_ws_handshake_status status = unison_ws_parse_handshake_response(
        (const uint8_t *)response, strlen(response), key, &header_len);

    CHECK(status == UNISON_WS_HANDSHAKE_OK);
    CHECK(header_len == strlen("HTTP/1.1 101 Switching Protocols\r\n"
                                "Upgrade: websocket\r\n"
                                "Connection: Upgrade\r\n"
                                "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
                                "\r\n"));

    /* Incomplete: no blank-line terminator yet. */
    const char *partial = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: web";
    status = unison_ws_parse_handshake_response((const uint8_t *)partial, strlen(partial), key,
                                                  &header_len);
    CHECK(status == UNISON_WS_HANDSHAKE_INCOMPLETE);

    /* Wrong accept value must be rejected, not just parsed leniently. */
    const char *bad_accept = "HTTP/1.1 101 Switching Protocols\r\n"
                              "Sec-WebSocket-Accept: not-the-right-value===\r\n"
                              "\r\n";
    status = unison_ws_parse_handshake_response((const uint8_t *)bad_accept, strlen(bad_accept),
                                                  key, &header_len);
    CHECK(status == UNISON_WS_HANDSHAKE_ERR);

    /* Non-101 status must be rejected even with a header block present. */
    const char *not_upgraded = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    status = unison_ws_parse_handshake_response((const uint8_t *)not_upgraded,
                                                  strlen(not_upgraded), key, &header_len);
    CHECK(status == UNISON_WS_HANDSHAKE_ERR);
}

static void test_frame_round_trip_unmasked(void) {
    /* Mirrors what GBAStreamHost actually sends: unmasked, FIN=1, opcode
     * binary, small payload -> single-byte length field. */
    uint8_t wire[] = {0x82, 0x03, 0x02, 0xAA, 0xBB, 0xFF /* trailing byte of a next frame */};

    unison_ws_frame frame;
    unison_ws_frame_status status = unison_ws_parse_frame(wire, sizeof(wire), &frame);

    CHECK(status == UNISON_WS_FRAME_OK);
    CHECK(frame.opcode == UNISON_WS_OPCODE_BINARY);
    CHECK(frame.payload_size == 3);
    CHECK(frame.payload[0] == 0x02);
    CHECK(frame.payload[1] == 0xAA);
    CHECK(frame.payload[2] == 0xBB);
    CHECK(frame.frame_size == 5); /* must not consume the trailing byte */
}

static void test_frame_incomplete(void) {
    uint8_t wire[] = {0x82, 0x05, 0x01, 0x02}; /* header claims 5 bytes, only 2 present */

    unison_ws_frame frame;
    CHECK(unison_ws_parse_frame(wire, sizeof(wire), &frame) == UNISON_WS_FRAME_INCOMPLETE);
}

static void test_frame_rejects_fragmentation(void) {
    uint8_t wire[] = {0x02, 0x01, 0xAA}; /* FIN=0, opcode=binary */

    unison_ws_frame frame;
    CHECK(unison_ws_parse_frame(wire, sizeof(wire), &frame) == UNISON_WS_FRAME_ERR);
}

static void test_build_frame_masks_and_round_trips(void) {
    const uint8_t payload[] = {0x02, 0x00, 0x00}; /* Unison input frame, no keys pressed */
    const uint8_t mask_key[4] = {0x11, 0x22, 0x33, 0x44};

    uint8_t out[32];
    size_t n = unison_ws_build_frame(UNISON_WS_OPCODE_BINARY, payload, sizeof(payload), mask_key,
                                       out, sizeof(out));

    CHECK(n == 2 + 4 + sizeof(payload)); /* header(2) + mask(4) + payload */
    CHECK(out[0] == 0x82);               /* FIN=1, opcode=binary */
    CHECK((out[1] & 0x80) != 0);         /* MASK bit set */
    CHECK((out[1] & 0x7F) == sizeof(payload));

    /* Parsing our own masked frame back out must recover the original
     * payload -- this is the exact shape our client sends for input
     * messages, so the round trip matters more than any individual field. */
    unison_ws_frame frame;
    CHECK(unison_ws_parse_frame(out, n, &frame) == UNISON_WS_FRAME_OK);
    CHECK(frame.payload_size == sizeof(payload));
    CHECK(memcmp(frame.payload, payload, sizeof(payload)) == 0);
}

int main(void) {
    test_generate_key();
    test_build_handshake_request();
    test_parse_handshake_response();
    test_frame_round_trip_unmasked();
    test_frame_incomplete();
    test_frame_rejects_fragmentation();
    test_build_frame_masks_and_round_trips();
    printf("websocket: all tests passed\n");
    return 0;
}
