#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Remixes arbitrary-channel-count/sample-rate PCM (as received in a
// UNISON_MSG_AUDIO frame) into interleaved 48000Hz stereo -- pulled out of
// PlayerActivity::playAudio() (player_activity.cpp) into its own free
// function, free of any borealis/libnx types (unlike the rest of
// player_activity.cpp), so it has one place to unit-test on a plain host
// compiler instead of only being exercisable via real audout playback --
// see tests/test_audio_remix.cpp.
//
// Switch's audout device is fixed at 48000Hz/stereo/s16 (audoutGetSampleRate()/
// audoutGetChannelCount()), unlike 3DS's NDSP (arbitrary rate) -- so unlike
// the 3DS client's equivalent (unison_3ds_remix_to_stereo(), which only
// remixes channels), this also linearly resamples when sampleRate != 48000.
// Mono duplication to both channels matches the 3DS client's own remix.
// Returns an empty vector for empty/malformed input (0 frames), same as
// the inline code this replaced.
std::vector<int16_t> unison_switch_remix_and_resample_to_stereo(const std::vector<int16_t> &pcm,
                                                                   uint32_t sampleRate, uint8_t channels);
