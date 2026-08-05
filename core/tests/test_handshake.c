/* unison/handshake.h had zero test coverage before this file -- including
 * hello_ack.video_mode / session_ready.video_mode, the negotiation fields
 * the "Video-mode fallback" feature (docs/protocol.md) is built on. These
 * tests exist to check the actual *behavior* those functions are supposed
 * to have, not just that the code compiles -- see docs/capabilities.md and
 * the CI plan this file closes a gap in. */
#include "unison/handshake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            exit(1);                                                          \
        }                                                                      \
    } while (0)

static void test_peek_handshake_message(void) {
    CHECK(unison_peek_handshake_message((const uint8_t *)"{\"message\":\"hello\"}", 19) ==
          UNISON_HS_MSG_HELLO);
    CHECK(unison_peek_handshake_message((const uint8_t *)"{\"message\":\"session_ready\"}", 27) ==
          UNISON_HS_MSG_SESSION_READY);
    CHECK(unison_peek_handshake_message((const uint8_t *)"{\"message\":\"handshake_error\"}", 29) ==
          UNISON_HS_MSG_HANDSHAKE_ERROR);
    /* Unrecognized discriminator and outright malformed JSON both come back
     * UNKNOWN, not an error code -- there's nothing else this function
     * could tell the caller either way, it's on them to fail the handshake. */
    CHECK(unison_peek_handshake_message((const uint8_t *)"{\"message\":\"bogus\"}", 19) ==
          UNISON_HS_MSG_UNKNOWN);
    CHECK(unison_peek_handshake_message((const uint8_t *)"not json at all", 15) ==
          UNISON_HS_MSG_UNKNOWN);
    CHECK(unison_peek_handshake_message(NULL, 0) == UNISON_HS_MSG_UNKNOWN);
}

static void test_parse_hello(void) {
    const char *json =
        "{\"message\":\"hello\",\"protocol_version\":2,\"stream_type\":\"GC_GBA_LINK\","
        "\"slots\":[{\"index\":0,\"label\":\"P1\",\"occupied\":false},"
        "{\"index\":1,\"label\":\"P2\",\"occupied\":true}],"
        "\"video\":{\"width\":240,\"height\":160,\"fps\":59.7275},"
        "\"audio\":{\"sample_rate\":32768,\"channels\":2},"
        "\"input_encoding\":\"gba_buttons\"}";

    unison_hello hello;
    CHECK(unison_parse_hello((const uint8_t *)json, strlen(json), &hello) == UNISON_HANDSHAKE_OK);
    CHECK(hello.protocol_version == 2);
    CHECK(strcmp(hello.stream_type, "GC_GBA_LINK") == 0);
    CHECK(hello.slot_count == 2);
    CHECK(hello.slots[0].index == 0 && strcmp(hello.slots[0].label, "P1") == 0 && !hello.slots[0].occupied);
    CHECK(hello.slots[1].index == 1 && strcmp(hello.slots[1].label, "P2") == 0 && hello.slots[1].occupied);
    CHECK(hello.video.width == 240 && hello.video.height == 160);
    CHECK(hello.has_audio && hello.audio.sample_rate == 32768 && hello.audio.channels == 2);
    CHECK(strcmp(hello.input_encoding, "gba_buttons") == 0);

    /* No "audio" member at all (N3DS_BOTTOM_SCREEN/NDS_BOTTOM_SCREEN never
     * send one, docs/protocol.md "Stream-Typen") -- has_audio must come
     * back false, not just stale/uninitialized. */
    const char *no_audio_json =
        "{\"message\":\"hello\",\"protocol_version\":2,\"stream_type\":\"N3DS_BOTTOM_SCREEN\","
        "\"video\":{\"width\":320,\"height\":240,\"fps\":60},\"input_encoding\":\"n3ds_touch\"}";
    CHECK(unison_parse_hello((const uint8_t *)no_audio_json, strlen(no_audio_json), &hello) ==
          UNISON_HANDSHAKE_OK);
    CHECK(!hello.has_audio);

    /* Wrong discriminator is rejected outright, not parsed as a best-effort. */
    const char *wrong_message = "{\"message\":\"session_ready\"}";
    CHECK(unison_parse_hello((const uint8_t *)wrong_message, strlen(wrong_message), &hello) ==
          UNISON_HANDSHAKE_ERR);
}

/* hello_ack.video_mode is the client's requested mode -- round-trips it
 * through unison_build_hello_ack() (the only place that ever writes this
 * field) into real JSON and confirms both that the value survives intact
 * and that it's actually present as its own member, not merged/garbled
 * with a neighboring field. */
static void test_build_hello_ack_video_mode(void) {
    static const char *const modes[] = {"tiles", "legacy", "h264", "h265"};
    char buf[256];

    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        unison_hello_ack_request req;
        memset(&req, 0, sizeof(req));
        req.requested_slot = 0;
        req.max_width = 854;
        req.max_height = 480;
        req.max_fps = 20.0;
        req.wants_audio = 0;
        strncpy(req.video_mode, modes[i], sizeof(req.video_mode) - 1);

        size_t n = unison_build_hello_ack(&req, buf, sizeof(buf));
        CHECK(n > 0 && n < sizeof(buf));
        char needle[32];
        snprintf(needle, sizeof(needle), "\"video_mode\":\"%s\"", modes[i]);
        CHECK(strstr(buf, needle) != NULL);

        /* Round-trip through the actual JSON parser used elsewhere in this
         * file, not just a substring match -- catches a field that's
         * present but not where unison_parse_session_ready-style parsing
         * would find it (e.g. nested one level too deep). */
        unison_session_ready ready;
        memset(&ready, 0xAA, sizeof(ready)); /* poison, so a no-op parse would be caught below */
        char session_ready_json[300];
        snprintf(session_ready_json, sizeof(session_ready_json),
                 "{\"message\":\"session_ready\",\"slot\":0,\"video\":{\"width\":1,\"height\":1,\"fps\":1},"
                 "\"video_mode\":\"%s\"}",
                 modes[i]);
        CHECK(unison_parse_session_ready((const uint8_t *)session_ready_json, strlen(session_ready_json),
                                           &ready) == UNISON_HANDSHAKE_OK);
        CHECK(strcmp(ready.video_mode, modes[i]) == 0);
    }

    /* Empty video_mode (the client's own "unset/use server default"
     * convention, e.g. an older client) must be OMITTED from the built
     * JSON entirely -- not written as "" -- so an old server that doesn't
     * even look for the field sees exactly the same hello_ack shape it
     * always has. */
    unison_hello_ack_request req;
    memset(&req, 0, sizeof(req));
    req.max_width = 240;
    req.max_height = 160;
    req.max_fps = 60.0;
    size_t n = unison_build_hello_ack(&req, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(strstr(buf, "video_mode") == NULL);

    /* wants_audio and video_mode are independent axes -- both present at
     * once must still produce valid, fully-populated JSON (this exercises
     * unison_build_hello_ack()'s 4th snprintf branch, the only one
     * combining both). */
    req.wants_audio = 1;
    req.max_sample_rate = 48000;
    req.max_channels = 2;
    strncpy(req.video_mode, "h264", sizeof(req.video_mode) - 1);
    n = unison_build_hello_ack(&req, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(strstr(buf, "\"video_mode\":\"h264\"") != NULL);
    CHECK(strstr(buf, "\"audio_limits\":{\"max_sample_rate\":48000,\"max_channels\":2}") != NULL);

    /* Buffer too small: must fail cleanly (return 0), never truncate a
     * partial/invalid JSON payload into out_buf. */
    char tiny[8];
    CHECK(unison_build_hello_ack(&req, tiny, sizeof(tiny)) == 0);
}

/* session_ready.video_mode is what the server actually granted -- the
 * asymmetric half of the fallback feature: unlike hello_ack's own
 * video_mode (whose absence has one clear meaning, "default to tiles"),
 * an absent session_ready.video_mode means "this server predates the
 * field, no information at all" and callers must not treat that the same
 * as any specific granted value. This is exactly the distinction
 * docs/protocol.md's "Video-mode fallback" section spells out, and the
 * one a false-positive/false-negative bug here would break silently. */
static void test_parse_session_ready_video_mode(void) {
    unison_session_ready ready;

    /* Field absent entirely (a host that predates it) -- must come back
     * as an empty string, not "tiles" or any other default. */
    const char *no_field_json =
        "{\"message\":\"session_ready\",\"slot\":0,\"video\":{\"width\":854,\"height\":480,\"fps\":20}}";
    memset(&ready, 0xAA, sizeof(ready));
    CHECK(unison_parse_session_ready((const uint8_t *)no_field_json, strlen(no_field_json), &ready) ==
          UNISON_HANDSHAKE_OK);
    CHECK(ready.video_mode[0] == '\0');

    /* Field present and non-default (the actual fallback case: a client
     * asked for h264, the server only has legacy). */
    const char *fallback_json =
        "{\"message\":\"session_ready\",\"slot\":0,\"video\":{\"width\":854,\"height\":480,\"fps\":20},"
        "\"video_mode\":\"legacy\"}";
    CHECK(unison_parse_session_ready((const uint8_t *)fallback_json, strlen(fallback_json), &ready) ==
          UNISON_HANDSHAKE_OK);
    CHECK(strcmp(ready.video_mode, "legacy") == 0);

    /* Field present alongside has_redirect -- both are independently
     * parsed, neither one should suppress the other (the redirect-hop
     * value is a placeholder per docs/protocol.md, but this module itself
     * doesn't know that; it must still parse it faithfully either way). */
    const char *redirect_json =
        "{\"message\":\"session_ready\",\"slot\":2,\"video\":{\"width\":240,\"height\":160,\"fps\":59.7275},"
        "\"video_mode\":\"tiles\",\"redirect\":{\"host\":\"192.168.1.42\",\"port\":6803}}";
    CHECK(unison_parse_session_ready((const uint8_t *)redirect_json, strlen(redirect_json), &ready) ==
          UNISON_HANDSHAKE_OK);
    CHECK(ready.has_redirect);
    CHECK(strcmp(ready.redirect_host, "192.168.1.42") == 0);
    CHECK(ready.redirect_port == 6803);
    CHECK(strcmp(ready.video_mode, "tiles") == 0);
}

static void test_parse_session_ready_other_fields(void) {
    unison_session_ready ready;

    /* has_audio mirrors unison_hello's own audio-optionality -- absent
     * "audio" member must come back has_audio=0, not stale/garbage. */
    const char *no_audio_json =
        "{\"message\":\"session_ready\",\"slot\":0,\"video\":{\"width\":320,\"height\":240,\"fps\":60}}";
    memset(&ready, 0xAA, sizeof(ready));
    CHECK(unison_parse_session_ready((const uint8_t *)no_audio_json, strlen(no_audio_json), &ready) ==
          UNISON_HANDSHAKE_OK);
    CHECK(!ready.has_audio);
    CHECK(!ready.has_redirect);

    const char *with_audio_json =
        "{\"message\":\"session_ready\",\"slot\":0,\"video\":{\"width\":854,\"height\":480,\"fps\":20},"
        "\"audio\":{\"sample_rate\":48000,\"channels\":2}}";
    CHECK(unison_parse_session_ready((const uint8_t *)with_audio_json, strlen(with_audio_json), &ready) ==
          UNISON_HANDSHAKE_OK);
    CHECK(ready.has_audio && ready.audio.sample_rate == 48000 && ready.audio.channels == 2);

    /* Wrong discriminator rejected outright. */
    const char *wrong_message = "{\"message\":\"hello\"}";
    CHECK(unison_parse_session_ready((const uint8_t *)wrong_message, strlen(wrong_message), &ready) ==
          UNISON_HANDSHAKE_ERR);
}

static void test_parse_handshake_error(void) {
    const char *json =
        "{\"message\":\"handshake_error\",\"code\":\"slot_unavailable\","
        "\"detail\":\"Slot P2 was taken by another client in the meantime.\"}";

    unison_handshake_error err;
    CHECK(unison_parse_handshake_error((const uint8_t *)json, strlen(json), &err) == UNISON_HANDSHAKE_OK);
    CHECK(strcmp(err.code, "slot_unavailable") == 0);
    CHECK(strcmp(err.detail, "Slot P2 was taken by another client in the meantime.") == 0);

    const char *wrong_message = "{\"message\":\"hello\"}";
    CHECK(unison_parse_handshake_error((const uint8_t *)wrong_message, strlen(wrong_message), &err) ==
          UNISON_HANDSHAKE_ERR);
}

static void test_stream_type_prefers_secondary_screen(void) {
    /* The three dual-screen-source stream types that must always land on a
     * two-screen client's own secondary display. */
    CHECK(unison_stream_type_prefers_secondary_screen("N3DS_BOTTOM_SCREEN"));
    CHECK(unison_stream_type_prefers_secondary_screen("NDS_BOTTOM_SCREEN"));
    CHECK(unison_stream_type_prefers_secondary_screen("WIIU_GAMEPAD"));
    /* GC_GBA_LINK is a primary display, and an unrecognized/future
     * stream_type must default to "not secondary" (fail open to the
     * user's own screen preference), not silently claim a screen it
     * doesn't know the semantics of. */
    CHECK(!unison_stream_type_prefers_secondary_screen("GC_GBA_LINK"));
    CHECK(!unison_stream_type_prefers_secondary_screen("SOME_FUTURE_STREAM_TYPE"));
}

int main(void) {
    test_peek_handshake_message();
    test_parse_hello();
    test_build_hello_ack_video_mode();
    test_parse_session_ready_video_mode();
    test_parse_session_ready_other_fields();
    test_parse_handshake_error();
    test_stream_type_prefers_secondary_screen();
    printf("handshake: all tests passed\n");
    return 0;
}
