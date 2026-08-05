#pragma once

#include <cstddef>
#include <cstdint>

// Remixes arbitrary-channel-count PCM (as received in a UNISON_MSG_AUDIO
// frame) into interleaved stereo -- pulled out of AudioPlayer::play()
// (audio.cpp) into its own free function, free of <3ds.h>/NDSP types
// (unlike the rest of audio.cpp), so it has one place to unit-test on a
// plain host compiler instead of only being exercisable via real NDSP
// playback -- see tests/test_audio_remix.cpp.
//
// pcm holds `frames * channels` interleaved samples; out must have room for
// `frames * 2` samples. channels >= 2 takes the first two channels as
// left/right verbatim (matches every currently-defined stream_type: none
// send more than stereo); channels == 1 duplicates the single channel to
// both left and right, same as the Switch client's own remix (see
// unison_switch_remix_and_resample_to_stereo()).
void unison_3ds_remix_to_stereo(const int16_t *pcm, std::size_t frames, uint8_t channels, int16_t *outStereo);
