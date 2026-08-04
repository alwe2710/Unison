/* Host-buildable (plain gcc/clang, no devkitARM) unit test for
 * finlink_nds_should_show_video_on_bottom() (screen_choice.h/.c) -- same
 * decision and same reasoning as the 3DS client's
 * shouldShowVideoOnBottomScreen(), see that file's own test for the fuller
 * comment. Links screen_choice.c + finlink_core directly, same pattern as
 * Cemu/melonDS's tests/test_finlink_messages.cpp. */

#include "screen_choice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            exit(1);                                                            \
        }                                                                         \
    } while (0)

static void test_gc_gba_link_respects_pref(void) {
    CHECK(finlink_nds_should_show_video_on_bottom(false, "GC_GBA_LINK") == false);
    CHECK(finlink_nds_should_show_video_on_bottom(true, "GC_GBA_LINK") == true);
}

static void test_modern_host_forces_bottom_regardless_of_pref(void) {
    const char *modern_host_types[] = { "N3DS_BOTTOM_SCREEN", "WIIU_GAMEPAD", "NDS_BOTTOM_SCREEN" };
    for (size_t i = 0; i < sizeof(modern_host_types) / sizeof(modern_host_types[0]); i++) {
        CHECK(finlink_nds_should_show_video_on_bottom(false, modern_host_types[i]) == true);
        CHECK(finlink_nds_should_show_video_on_bottom(true, modern_host_types[i]) == true);
    }
}

static void test_unknown_stream_type_falls_back_to_pref(void) {
    CHECK(finlink_nds_should_show_video_on_bottom(false, "SOME_FUTURE_STREAM_TYPE") == false);
    CHECK(finlink_nds_should_show_video_on_bottom(true, "SOME_FUTURE_STREAM_TYPE") == true);
}

int main(void) {
    test_gc_gba_link_respects_pref();
    test_modern_host_forces_bottom_regardless_of_pref();
    test_unknown_stream_type_falls_back_to_pref();
    printf("dual_screen_choice (nds): all tests passed\n");
    return 0;
}
