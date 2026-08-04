#include "audio_remix.hpp"

void finlink_3ds_remix_to_stereo(const int16_t *pcm, std::size_t frames, uint8_t channels, int16_t *outStereo) {
    for (std::size_t i = 0; i < frames; i++) {
        int16_t l = pcm[i * channels];
        int16_t r = channels >= 2 ? pcm[i * channels + 1] : l;
        outStereo[i * 2] = l;
        outStereo[i * 2 + 1] = r;
    }
}
