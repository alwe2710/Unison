#include "h264_decoder.hpp"

#include <cstring>
#include <utility>

namespace {
// Generous fixed cap for one NAL unit's raw bytes -- matches
// devkitPro/3ds-examples' own mvd example sizing (that example uses
// 0x100000/1MB input/output staging buffers; a single NAL from this
// project's own encoders, capped well under typical GBA/3DS/DS stream
// resolutions, comfortably fits well inside this).
constexpr size_t kInputBufferCapacity = 512 * 1024;
} // namespace

H264Decoder::H264Decoder(uint32_t inputWidth, uint32_t inputHeight) : width(inputWidth), height(inputHeight) {
    if (width == 0 || height == 0) {
        return;
    }

    // MVDMODE_VIDEOPROCESSING (not MVDMODE_COLORFORMATCONV, that mode is
    // for converting an already-decoded YUYV422 buffer, not for decoding a
    // compressed bitstream at all): MVD_INPUT_H264 in, MVD_OUTPUT_RGB565
    // out -- RGB565, not BGR565, so this needs no channel-swap before
    // handing pixels to VideoTex::setFrame(), which already expects
    // citro3d's GPU_RGB565 layout directly (see that class's own comment).
    if (R_FAILED(mvdstdInit(MVDMODE_VIDEOPROCESSING, MVD_INPUT_H264, MVD_OUTPUT_RGB565, MVD_DEFAULT_WORKBUF_SIZE,
                            nullptr))) {
        // Old 3DS/2DS (no MVD hardware) fails here -- see this class's own
        // header comment on why that's not distinguished from any other
        // init failure.
        return;
    }

    inputBuffer = linearAlloc(kInputBufferCapacity);
    outputBufferSize = static_cast<size_t>(width) * height * 2;
    outputBuffer = linearAlloc(outputBufferSize);
    if (!inputBuffer || !outputBuffer) {
        if (inputBuffer) {
            linearFree(inputBuffer);
            inputBuffer = nullptr;
        }
        if (outputBuffer) {
            linearFree(outputBuffer);
            outputBuffer = nullptr;
        }
        mvdstdExit();
        return;
    }
    inputBufferCapacity = kInputBufferCapacity;

    // vaddr_colorconv_indata: NULL, irrelevant for MVDMODE_VIDEOPROCESSING
    // (only meaningful for the color-conversion mode this class never
    // uses). vaddr_outdata0/vaddr_outdata1: the same single buffer for
    // both, matching the official example (no double-buffering here --
    // this class's own decode() already copies the result out into
    // outRgb565 before the caller could possibly need a second in-flight
    // buffer).
    mvdstdGenerateDefaultConfig(&config, width, height, width, height, nullptr,
                                static_cast<u32 *>(outputBuffer), static_cast<u32 *>(outputBuffer));

    initialized = true;
}

H264Decoder::~H264Decoder() {
    if (inputBuffer) {
        linearFree(inputBuffer);
    }
    if (outputBuffer) {
        linearFree(outputBuffer);
    }
    if (initialized) {
        mvdstdExit();
    }
}

bool H264Decoder::decode(const uint8_t *data, size_t len, std::vector<uint8_t> &outRgb565) {
    if (!initialized || len == 0) {
        return false;
    }

    // Splits Annex-B into individual NAL units (start codes stripped),
    // handling both 3-byte (00 00 01) and 4-byte (00 00 00 01) start
    // codes -- same algorithm as the sibling iOS client's
    // CompressedVideoDecoder.splitAnnexB() (see that function's own
    // comment for why both forms need handling: x264's b_annexb=1 output,
    // which every host repo's SoftwareVideoEncoder uses, doesn't
    // consistently pick one). Unlike that iOS parser, this doesn't need to
    // separate parameter-set NALs from slice NALs itself -- MVD's own
    // mvdstdProcessVideoFrame() handles SPS/PPS NALs internally (returning
    // MVD_STATUS_PARAMSET for those, checked below) the same way it
    // handles slice NALs, so every extracted NAL is simply fed through in
    // order.
    std::vector<std::pair<size_t, size_t>> nalRanges; // (offset, length) into data
    {
        size_t i = 0;
        std::vector<size_t> starts;
        while (i + 2 < len) {
            if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
                starts.push_back(i + 3);
                i += 3;
            } else {
                i++;
            }
        }
        for (size_t idx = 0; idx < starts.size(); idx++) {
            size_t start = starts[idx];
            size_t end = idx + 1 < starts.size() ? starts[idx + 1] - 3 : len;
            while (end > start && data[end - 1] == 0) {
                end--;
            }
            if (end > start) {
                nalRanges.emplace_back(start, end - start);
            }
        }
    }
    if (nalRanges.empty()) {
        return false;
    }

    bool frameReady = false;
    for (const auto &[offset, nalLen] : nalRanges) {
        if (nalLen == 0 || nalLen > inputBufferCapacity) {
            continue;
        }
        std::memcpy(inputBuffer, data + offset, nalLen);
        GSPGPU_FlushDataCache(inputBuffer, nalLen);

        MVDSTD_ProcessNALUnitOut procOut {};
        const Result processResult = mvdstdProcessVideoFrame(inputBuffer, static_cast<u32>(nalLen), 0, &procOut);
        if (!MVD_CHECKNALUPROC_SUCCESS(processResult)) {
            continue;
        }
        // MVD_STATUS_PARAMSET: this NAL was just SPS/PPS, no picture data
        // to render yet. MVD_STATUS_INCOMPLETEPROCESSING: this NAL alone
        // doesn't complete a picture (more slice data needed from a
        // following NAL) -- neither means a frame is ready. Anything else
        // (including the ordinary success case) means this NAL completed
        // a picture, matching the official mvd example's own check.
        if (processResult == MVD_STATUS_PARAMSET || processResult == MVD_STATUS_INCOMPLETEPROCESSING) {
            continue;
        }
        if (R_FAILED(mvdstdRenderVideoFrame(&config, true))) {
            continue;
        }
        frameReady = true;
    }

    if (!frameReady) {
        return false;
    }

    GSPGPU_InvalidateDataCache(outputBuffer, outputBufferSize);
    outRgb565.resize(outputBufferSize);
    std::memcpy(outRgb565.data(), outputBuffer, outputBufferSize);
    return true;
}
