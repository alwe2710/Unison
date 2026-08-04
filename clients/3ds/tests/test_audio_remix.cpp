// Host-buildable (plain g++/clang++, no devkitARM) unit test for
// finlink_3ds_remix_to_stereo() (audio_remix.hpp/.cpp) -- the
// dual-audio-client test category's "Audiosignal wird empfangen und
// wiedergegeben" check for the 3DS client's stereo remix step.

#include "audio_remix.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

#define CHECK(cond)                                                                    \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            std::exit(1);                                                               \
        }                                                                                \
    } while (0)

static void TestMonoDuplicatesToBothChannels() {
    const std::vector<int16_t> mono = {100, -200, 300};
    std::vector<int16_t> out(mono.size() * 2);
    finlink_3ds_remix_to_stereo(mono.data(), mono.size(), 1, out.data());
    CHECK(out[0] == 100 && out[1] == 100);
    CHECK(out[2] == -200 && out[3] == -200);
    CHECK(out[4] == 300 && out[5] == 300);
}

static void TestStereoPassesThroughVerbatim() {
    // Interleaved L/R already -- must come out identical, not swapped or
    // averaged.
    const std::vector<int16_t> stereo = {10, -10, 20, -20};
    std::vector<int16_t> out(stereo.size());
    finlink_3ds_remix_to_stereo(stereo.data(), 2, 2, out.data());
    CHECK(out[0] == 10 && out[1] == -10);
    CHECK(out[2] == 20 && out[3] == -20);
}

static void TestExtraChannelsBeyondStereoAreIgnored() {
    // No currently-defined stream_type sends more than stereo (see
    // audio_remix.hpp's own comment), but the function must still only
    // ever take the first two channels of whatever it's given, not read
    // out of bounds or silently mix in extra channels.
    const std::vector<int16_t> quad = {1, 2, 3, 4, 5, 6, 7, 8};  // 2 frames, 4 channels
    std::vector<int16_t> out(4);
    finlink_3ds_remix_to_stereo(quad.data(), 2, 4, out.data());
    CHECK(out[0] == 1 && out[1] == 2);
    CHECK(out[2] == 5 && out[3] == 6);
}

int main() {
    TestMonoDuplicatesToBothChannels();
    TestStereoPassesThroughVerbatim();
    TestExtraChannelsBeyondStereoAreIgnored();
    std::printf("audio_remix (3ds): all tests passed\n");
    return 0;
}
