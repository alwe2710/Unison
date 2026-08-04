// Host-buildable (plain g++/clang++, no devkitARM) unit test for
// shouldShowVideoOnBottomScreen() (prefs.hpp/.cpp) -- the dual-screen-client
// touchscreen-forcing / free-screen-choice decision, see prefs.hpp's own
// comment on the function. Links prefs.cpp + finlink_core directly, same
// pattern as Cemu/melonDS's tests/test_finlink_messages.cpp.

#include "prefs.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#define CHECK(cond)                                                                    \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            std::exit(1);                                                               \
        }                                                                                \
    } while (0)

static void TestDolphinRespectsPref() {
    // GC_GBA_LINK (dolphin) is a single-screen source -- the user's own
    // pref is the only thing that decides, both ways.
    CHECK(shouldShowVideoOnBottomScreen(false, "GC_GBA_LINK") == false);
    CHECK(shouldShowVideoOnBottomScreen(true, "GC_GBA_LINK") == true);
}

static void TestModernHostForcesBottomRegardlessOfPref() {
    // N3DS_BOTTOM_SCREEN/WIIU_GAMEPAD/NDS_BOTTOM_SCREEN are all dual-screen
    // sources' own secondary screen -- forced to bottom even when the user's
    // own pref says top, the exact false-positive/false-negative case this
    // whole check exists for.
    for (const std::string streamType : {"N3DS_BOTTOM_SCREEN", "WIIU_GAMEPAD", "NDS_BOTTOM_SCREEN"}) {
        CHECK(shouldShowVideoOnBottomScreen(false, streamType) == true);
        CHECK(shouldShowVideoOnBottomScreen(true, streamType) == true);
    }
}

static void TestUnknownStreamTypeFallsBackToPref() {
    // Neither a known dual-screen secondary nor GC_GBA_LINK -- must not
    // silently force bottom just because it's unrecognized.
    CHECK(shouldShowVideoOnBottomScreen(false, "SOME_FUTURE_STREAM_TYPE") == false);
    CHECK(shouldShowVideoOnBottomScreen(true, "SOME_FUTURE_STREAM_TYPE") == true);
}

int main() {
    TestDolphinRespectsPref();
    TestModernHostForcesBottomRegardlessOfPref();
    TestUnknownStreamTypeFallsBackToPref();
    std::printf("dual_screen_choice (3ds): all tests passed\n");
    return 0;
}
