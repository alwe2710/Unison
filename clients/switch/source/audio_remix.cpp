#include "audio_remix.hpp"

#include <algorithm>

std::vector<int16_t> finlink_switch_remix_and_resample_to_stereo(const std::vector<int16_t> &pcm,
                                                                   uint32_t sampleRate, uint8_t channels) {
    if (sampleRate == 0 || channels == 0 || pcm.empty()) {
        return {};
    }

    size_t frames = pcm.size() / channels;
    if (frames == 0) {
        return {};
    }

    std::vector<int16_t> stereo(frames * 2);
    for (size_t i = 0; i < frames; i++) {
        int16_t l = pcm[i * channels];
        int16_t r = channels >= 2 ? pcm[i * channels + 1] : l;
        stereo[i * 2] = l;
        stereo[i * 2 + 1] = r;
    }

    if (sampleRate == 48000) {
        return stereo;
    }

    std::vector<int16_t> resampled;
    size_t outFrames = static_cast<size_t>(frames) * 48000 / sampleRate;
    resampled.resize(outFrames * 2);
    for (size_t i = 0; i < outFrames; i++) {
        double srcPos = static_cast<double>(i) * sampleRate / 48000.0;
        size_t i0 = static_cast<size_t>(srcPos);
        size_t i1 = std::min(i0 + 1, frames - 1);
        double frac = srcPos - static_cast<double>(i0);
        for (int c = 0; c < 2; c++) {
            double v = stereo[i0 * 2 + c] * (1.0 - frac) + stereo[i1 * 2 + c] * frac;
            resampled[i * 2 + c] = static_cast<int16_t>(v);
        }
    }
    return resampled;
}
