#include "audio.hpp"

#include <cstring>

#include "audio_remix.hpp"

AudioPlayer::AudioPlayer() {
    if (R_SUCCEEDED(ndspInit())) {
        open = true;
        ndspSetOutputMode(NDSP_OUTPUT_STEREO);
        ndspChnReset(kChannel);
        ndspChnSetInterp(kChannel, NDSP_INTERP_LINEAR);
        ndspChnSetFormat(kChannel, NDSP_FORMAT_STEREO_PCM16);
        float mix[12] = { 0 };
        mix[0] = 1.0f; // front-left
        mix[1] = 1.0f; // front-right
        ndspChnSetMix(kChannel, mix);
    }
}

AudioPlayer::~AudioPlayer() {
    if (!open) {
        return;
    }
    ndspChnReset(kChannel);
    reclaim();
    ndspExit();
}

void AudioPlayer::reclaim() {
    for (size_t i = 0; i < pending.size();) {
        if (pending[i]->wavebuf.status == NDSP_WBUF_DONE) {
            linearFree(pending[i]->linearData);
            delete pending[i];
            pending.erase(pending.begin() + static_cast<long>(i));
        } else {
            i++;
        }
    }
}

void AudioPlayer::play(uint32_t sampleRate, uint8_t channels, std::vector<int16_t> pcm) {
    if (!open || sampleRate == 0 || channels == 0 || pcm.empty()) {
        return;
    }

    reclaim();

    if (sampleRate != currentRate) {
        ndspChnSetRate(kChannel, static_cast<float>(sampleRate));
        currentRate = sampleRate;
    }

    size_t frames = pcm.size() / channels;
    if (frames == 0) {
        return;
    }

    auto *buf = new PendingBuffer();
    buf->linearData = static_cast<int16_t *>(linearAlloc(frames * 2 * sizeof(int16_t)));
    if (!buf->linearData) {
        delete buf;
        return;
    }

    finlink_3ds_remix_to_stereo(pcm.data(), frames, channels, buf->linearData);
    DSP_FlushDataCache(buf->linearData, frames * 2 * sizeof(int16_t));

    buf->wavebuf.data_pcm16 = buf->linearData;
    buf->wavebuf.nsamples = static_cast<u32>(frames);
    buf->wavebuf.looping = false;
    buf->wavebuf.status = NDSP_WBUF_FREE;

    pending.push_back(buf);
    ndspChnWaveBufAdd(kChannel, &buf->wavebuf);
}
