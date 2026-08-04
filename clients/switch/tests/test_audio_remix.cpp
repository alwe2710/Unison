// Host-buildable (plain g++/clang++, no devkitA64) unit test for
// finlink_switch_remix_and_resample_to_stereo() (audio_remix.hpp/.cpp) --
// the dual-audio-client test category's "Audiosignal wird empfangen und
// wiedergegeben" check for the Switch client's remix+resample step.

#include "audio_remix.hpp"

#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                                    \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            std::exit(1);                                                               \
        }                                                                                \
    } while (0)

static void TestAlreadyNativeRateSkipsResampling() {
    // sampleRate == 48000 -- only remix, frame count must stay unchanged
    // (no up/downsampling math applied at all).
    const std::vector<int16_t> mono = {100, -200, 300};
    auto out = finlink_switch_remix_and_resample_to_stereo(mono, 48000, 1);
    CHECK(out.size() == mono.size() * 2);
    CHECK(out[0] == 100 && out[1] == 100);
    CHECK(out[2] == -200 && out[3] == -200);
    CHECK(out[4] == 300 && out[5] == 300);
}

static void TestMonoDuplicatesToBothChannels() {
    const std::vector<int16_t> mono = {42};
    auto out = finlink_switch_remix_and_resample_to_stereo(mono, 48000, 1);
    CHECK(out.size() == 2);
    CHECK(out[0] == 42 && out[1] == 42);
}

static void TestStereoPassesThroughVerbatimAtNativeRate() {
    const std::vector<int16_t> stereo = {10, -10, 20, -20};
    auto out = finlink_switch_remix_and_resample_to_stereo(stereo, 48000, 2);
    CHECK(out.size() == 4);
    CHECK(out[0] == 10 && out[1] == -10);
    CHECK(out[2] == 20 && out[3] == -20);
}

static void TestUpsamplingDoublesFrameCount() {
    // 24000Hz -> 48000Hz is an exact 2x upsample -- output frame count must
    // be exactly double the input's.
    const std::vector<int16_t> mono = {0, 1000, 2000, 3000};
    auto out = finlink_switch_remix_and_resample_to_stereo(mono, 24000, 1);
    CHECK(out.size() == mono.size() * 2 * 2);  // *2 for stereo, *2 for 2x upsample
}

static void TestUpsamplingInterpolatesBetweenSamples() {
    // Exact-midpoint interpolation case: 24000Hz source with samples 0 and
    // 1000 -- the 48kHz output's second sample should land exactly halfway
    // between them (linear interpolation, not nearest-neighbor).
    const std::vector<int16_t> mono = {0, 1000};
    auto out = finlink_switch_remix_and_resample_to_stereo(mono, 24000, 1);
    // outFrames = 2 * 48000 / 24000 = 4 frames
    CHECK(out.size() == 8);
    CHECK(out[0] == 0);       // frame 0: srcPos 0.0 -> sample 0 exactly
    CHECK(out[2] == 500);     // frame 1: srcPos 0.5 -> halfway between 0 and 1000
    CHECK(out[4] == 1000);    // frame 2: srcPos 1.0 -> sample 1 exactly
}

static void TestDownsamplingReducesFrameCount() {
    // 96000Hz -> 48000Hz is an exact 2x downsample.
    std::vector<int16_t> mono(8, 0);
    for (size_t i = 0; i < mono.size(); i++) {
        mono[i] = static_cast<int16_t>(i * 100);
    }
    auto out = finlink_switch_remix_and_resample_to_stereo(mono, 96000, 1);
    CHECK(out.size() == 8);  // 4 output frames * 2 channels
}

static void TestResampleDoesNotReadPastLastFrame() {
    // The last output frame's interpolation would naively want
    // src_frame+1, which doesn't exist for the actual last source frame --
    // must clamp rather than read out of bounds (see the i1 = min(...)
    // clamp in the implementation). A crash or garbage value here is
    // exactly the bug this test exists to catch.
    const std::vector<int16_t> mono = {10, 20, 30};
    auto out = finlink_switch_remix_and_resample_to_stereo(mono, 44100, 1);
    CHECK(!out.empty());
    // Last frame's value should be close to the last source sample (30),
    // not garbage from reading past the buffer.
    CHECK(out[out.size() - 2] >= 20 && out[out.size() - 2] <= 30);
}

static void TestEmptyOrZeroInputReturnsEmpty() {
    CHECK(finlink_switch_remix_and_resample_to_stereo({}, 48000, 2).empty());
    CHECK(finlink_switch_remix_and_resample_to_stereo({1, 2, 3}, 0, 1).empty());
    CHECK(finlink_switch_remix_and_resample_to_stereo({1, 2, 3}, 48000, 0).empty());
}

int main() {
    TestAlreadyNativeRateSkipsResampling();
    TestMonoDuplicatesToBothChannels();
    TestStereoPassesThroughVerbatimAtNativeRate();
    TestUpsamplingDoublesFrameCount();
    TestUpsamplingInterpolatesBetweenSamples();
    TestDownsamplingReducesFrameCount();
    TestResampleDoesNotReadPastLastFrame();
    TestEmptyOrZeroInputReturnsEmpty();
    std::printf("audio_remix (switch): all tests passed\n");
    return 0;
}
