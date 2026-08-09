#include "h264_decoder.hpp"

#include <cstdio>
#include <cstring>
#include <utility>

namespace {
// Generous fixed cap for one NAL unit's raw bytes -- matches
// devkitPro/3ds-examples' own mvd example sizing (that example uses
// 0x100000/1MB input/output staging buffers; a single NAL from this
// project's own encoders, capped well under typical GBA/3DS/DS stream
// resolutions, comfortably fits well inside this).
constexpr size_t kInputBufferCapacity = 512 * 1024;

// TEMPORARY diagnostic aid for tracking down a real hardware crash inside
// the "mvd" sysmodule itself (data abort, reported live on New3DS) that
// two independent fix attempts (Annex-B start-code prefix; see decode()'s
// own comment) didn't resolve -- the crash reproduces byte-for-byte
// identically both times, meaning it happens deterministically, but it's
// still unknown *which* MVD call is the one actually crashing. Opens,
// appends one line, flushes, and closes on every call so whatever was
// logged last is durably on the SD card even if the very next libctru
// call is the one that takes the whole mvd process down with it -- an
// in-memory log or a log only flushed at the end would lose exactly the
// line that matters most. Remove once the real crash site is identified
// and fixed; this isn't meant to ship as permanent logging.
void MvdLog(const char *msg) {
    FILE *f = std::fopen("sdmc:/unison_mvd_log.txt", "a");
    if (!f) {
        return;
    }
    std::fputs(msg, f);
    std::fputc('\n', f);
    std::fflush(f);
    std::fclose(f);
}
} // namespace

H264Decoder::H264Decoder(uint32_t inputWidth, uint32_t inputHeight) : width(inputWidth), height(inputHeight) {
    char line[128];
    std::snprintf(line, sizeof(line), "ctor: width=%u height=%u", inputWidth, inputHeight);
    MvdLog(line);

    if (width == 0 || height == 0) {
        MvdLog("ctor: zero dimension, bailing before mvdstdInit");
        return;
    }

    // MVDMODE_VIDEOPROCESSING (not MVDMODE_COLORFORMATCONV, that mode is
    // for converting an already-decoded YUYV422 buffer, not for decoding a
    // compressed bitstream at all): MVD_INPUT_H264 in, MVD_OUTPUT_RGB565
    // out -- RGB565, not BGR565, so this needs no channel-swap before
    // handing pixels to VideoTex::setFrame(), which already expects
    // citro3d's GPU_RGB565 layout directly (see that class's own comment).
    MvdLog("calling mvdstdInit");
    const Result initResult =
        mvdstdInit(MVDMODE_VIDEOPROCESSING, MVD_INPUT_H264, MVD_OUTPUT_RGB565, MVD_DEFAULT_WORKBUF_SIZE, nullptr);
    std::snprintf(line, sizeof(line), "mvdstdInit returned 0x%08lx", static_cast<unsigned long>(initResult));
    MvdLog(line);
    if (R_FAILED(initResult)) {
        // Old 3DS/2DS (no MVD hardware) fails here -- see this class's own
        // header comment on why that's not distinguished from any other
        // init failure.
        return;
    }

    inputBuffer = linearAlloc(kInputBufferCapacity);
    outputBufferSize = static_cast<size_t>(width) * height * 2;
    outputBuffer = linearAlloc(outputBufferSize);
    std::snprintf(line, sizeof(line), "linearAlloc: inputBuffer=%p outputBuffer=%p outputBufferSize=%zu",
                  inputBuffer, outputBuffer, outputBufferSize);
    MvdLog(line);
    if (!inputBuffer || !outputBuffer) {
        MvdLog("linearAlloc failed, tearing down");
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
    MvdLog("calling mvdstdGenerateDefaultConfig");
    mvdstdGenerateDefaultConfig(&config, width, height, width, height, nullptr,
                                static_cast<u32 *>(outputBuffer), static_cast<u32 *>(outputBuffer));
    MvdLog("mvdstdGenerateDefaultConfig returned");

    initialized = true;
    MvdLog("ctor: initialized = true");
}

H264Decoder::~H264Decoder() {
    MvdLog("dtor: start");
    if (inputBuffer) {
        linearFree(inputBuffer);
    }
    if (outputBuffer) {
        linearFree(outputBuffer);
    }
    if (initialized) {
        mvdstdExit();
    }
    MvdLog("dtor: done");
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

    // mvdstdProcessVideoFrame()'s own doc comment is explicit: "Input
    // NAL-unit starting with the 3-byte '00 00 01' prefix." The Annex-B
    // splitter above deliberately strips start codes (offset points past
    // them, for the sibling iOS/web parsers that want bare NAL payloads),
    // so that prefix has to be reconstructed here -- feeding MVD a buffer
    // without it isn't just "wrong data", the sysmodule appears to
    // misparse the resulting bytes as its own internal header/offset
    // fields, which is what produced the real hardware data-abort crash
    // inside the "mvd" process itself (not this app's process) that this
    // fix addresses.
    bool frameReady = false;
    for (const auto &[offset, nalLen] : nalRanges) {
        if (nalLen == 0 || nalLen + 3 > inputBufferCapacity) {
            continue;
        }
        static const uint8_t kStartCode[3] = {0, 0, 1};
        std::memcpy(inputBuffer, kStartCode, 3);
        std::memcpy(static_cast<uint8_t *>(inputBuffer) + 3, data + offset, nalLen);
        const size_t bufLen = nalLen + 3;
        GSPGPU_FlushDataCache(inputBuffer, bufLen);

        MVDSTD_ProcessNALUnitOut procOut {};
        const Result processResult = mvdstdProcessVideoFrame(inputBuffer, static_cast<u32>(bufLen), 0, &procOut);
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
