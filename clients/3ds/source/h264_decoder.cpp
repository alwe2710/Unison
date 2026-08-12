#include "h264_decoder.hpp"

#include <cstdio>
#include <cstring>
#include <string>
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
        MvdLog("ctor: zero dimension, will never initialize");
        initFailed = true;
        return;
    }
    // Real init (mvdstdInit etc.) happens lazily on decode()'s first call --
    // see completeInit()'s own comment on why the real H.264 level, which
    // only a real SPS NAL carries, is needed first.
    MvdLog("ctor: deferring real init to decode()'s first SPS");
}

namespace {
// Maps a raw H.264 SPS level_idc byte (the 4th byte of an SPS NAL: 1 byte
// NAL header + profile_idc + constraint-flags + level_idc) to libctru's
// MVD_H264_LEVEL_* enum. Values are the literal level_idc byte (level N.M
// encodes as N*10+M, e.g. level 3.2 -> 32) -- confirmed against
// Core-2-Extreme/Video_player_for_3DS's own identical switch (that project
// gets the same numbers from ffmpeg's AVCodecContext::level instead of
// parsing SPS directly, but for ordinary H.264 that field is just the raw
// level_idc passed through). Deliberately not special-cased: level_idc=11
// with constraint_set3_flag=1 technically means "level 1.0b" rather than
// "level 1.1" -- an antique baseline-profile corner case no encoder in this
// project's own host repos (x264 at normal streaming resolutions) will
// ever emit, so it's mapped as plain 1.1 here rather than adding a second
// SPS byte read to disambiguate it.
bool mapLevelIdcToMvd(uint8_t levelIdc, u8 *out) {
    switch (levelIdc) {
    case 10: *out = MVD_H264_LEVEL_1_0; return true;
    case 11: *out = MVD_H264_LEVEL_1_1; return true;
    case 12: *out = MVD_H264_LEVEL_1_2; return true;
    case 13: *out = MVD_H264_LEVEL_1_3; return true;
    case 20: *out = MVD_H264_LEVEL_2_0; return true;
    case 21: *out = MVD_H264_LEVEL_2_1; return true;
    case 22: *out = MVD_H264_LEVEL_2_2; return true;
    case 30: *out = MVD_H264_LEVEL_3_0; return true;
    case 31: *out = MVD_H264_LEVEL_3_1; return true;
    case 32: *out = MVD_H264_LEVEL_3_2; return true;
    case 40: *out = MVD_H264_LEVEL_4_0; return true;
    case 41: *out = MVD_H264_LEVEL_4_1; return true;
    case 42: *out = MVD_H264_LEVEL_4_2; return true;
    case 50: *out = MVD_H264_LEVEL_5_0; return true;
    case 51: *out = MVD_H264_LEVEL_5_1; return true;
    case 52: *out = MVD_H264_LEVEL_5_2; return true;
    default: return false;
    }
}
} // namespace

bool H264Decoder::completeInit(uint32_t workBufSize) {
    char line[128];
    // MVDMODE_VIDEOPROCESSING (not MVDMODE_COLORFORMATCONV, that mode is
    // for converting an already-decoded YUYV422 buffer, not for decoding a
    // compressed bitstream at all): MVD_INPUT_H264 in, MVD_OUTPUT_BGR565
    // out. Was MVD_OUTPUT_RGB565 (this class's original comment here
    // claimed that needed no channel-swap before VideoTex::setFrame(),
    // which expects citro3d's GPU_RGB565 layout) -- changed after a real
    // hardware report of black/gray stripes that a render-result-vs-actual-
    // write sentinel check didn't fix (meaning MVD genuinely writes data
    // every call, just wrong-looking data, not a stale-buffer issue).
    // Every real MVD user found while researching this -- the official
    // devkitPro/3ds-examples mvd example AND Core-2-Extreme/
    // Video_player_for_3DS, an actively maintained, real-world working
    // player -- requests MVD_OUTPUT_BGR565, never RGB565; nothing found
    // anywhere uses RGB565 in practice. decode()'s own final copy now
    // swaps R/B per pixel before handing pixels to VideoTex, which still
    // expects RGB565.
    std::snprintf(line, sizeof(line), "completeInit: calling mvdstdInit workBufSize=%u", workBufSize);
    MvdLog(line);
    const Result initResult =
        mvdstdInit(MVDMODE_VIDEOPROCESSING, MVD_INPUT_H264, MVD_OUTPUT_BGR565, workBufSize, nullptr);
    std::snprintf(line, sizeof(line), "mvdstdInit returned 0x%08lx", static_cast<unsigned long>(initResult));
    MvdLog(line);
    if (R_FAILED(initResult)) {
        // Old 3DS/2DS (no MVD hardware) fails here -- see this class's own
        // header comment on why that's not distinguished from any other
        // init failure.
        initFailed = true;
        return false;
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
        initFailed = true;
        return false;
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
    MvdLog("completeInit: initialized = true");
    return true;
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

namespace {
// Batches this call's whole diagnostic log into one string, flushed to the
// SD card exactly once (via MvdLog(), still one open/write/flush/close) no
// matter which of decode()'s several return points fires -- was one
// separate MvdLog() call (one full file open/write/flush/close each) per
// logged line, ~15-20 of them per single decode() call once the crash
// itself was fixed and this function actually reached its per-NAL/per-
// render logging every frame instead of crashing after 2-3 lines. That
// much synchronous SD-card I/O on the session's own receive thread was
// slow enough to fall behind the incoming stream and trip a real
// connection timeout (reported directly: "bricht nach ca 10 sek ab").
// Batching keeps the same diagnostic content but costs one SD-card file
// operation per decoded frame instead of fifteen-plus.
struct LogBatch {
    std::string buf;
    void add(const char *msg) {
        buf += msg;
        buf += '\n';
    }
    ~LogBatch() {
        if (!buf.empty()) {
            MvdLog(buf.c_str());
        }
    }
};
} // namespace

bool H264Decoder::decode(const uint8_t *data, size_t len, std::vector<uint8_t> &outRgb565) {
    LogBatch log;
    {
        char line[64];
        std::snprintf(line, sizeof(line), "decode: entered, initialized=%d len=%zu", initialized ? 1 : 0, len);
        log.add(line);
    }
    if (initFailed || len == 0) {
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

    if (!initialized) {
        // Real init deferred from the constructor to right here -- see
        // completeInit()'s own comment. Needs a real SPS (nal_unit_type==7)
        // to read the stream's actual H.264 level (level_idc, the 4th byte
        // of the NAL: header + profile_idc + constraint-flags + level_idc)
        // for mvdstdCalculateBufferSize(), matching Core-2-Extreme/
        // Video_player_for_3DS's own real init -- their project computes a
        // properly-sized work buffer from the real level rather than using
        // this class's previous fixed MVD_DEFAULT_WORKBUF_SIZE (libctru's
        // own comment calls that one a "New3DS Internet Browser" default,
        // not something tuned for this stream's actual resolution/level).
        // Per docs/protocol.md's "Keyframe discipline", the very first
        // message a fresh session ever sees is a keyframe with SPS/PPS
        // ahead of slice data, so this should succeed on the first call in
        // practice; if some message genuinely has no SPS yet (mid-stream
        // reconnect edge case), this just drops it and waits for the next
        // one, same "next forced keyframe self-heals" spirit as every other
        // drop path in this function.
        const uint8_t *spsData = nullptr;
        size_t spsLen = 0;
        for (const auto &[offset, nalLen] : nalRanges) {
            if (nalLen > 0 && (data[offset] & 0x1F) == 7) {
                spsData = data + offset;
                spsLen = nalLen;
                break;
            }
        }
        if (!spsData) {
            log.add("decode: not yet initialized and no SPS in this message, dropping");
            return false;
        }

        u32 workBufSize = MVD_DEFAULT_WORKBUF_SIZE;
        u8 mvdLevel = 0;
        if (spsLen > 3 && mapLevelIdcToMvd(spsData[3], &mvdLevel)) {
            MVDSTD_CalculateWorkBufSizeConfig calcConfig {};
            calcConfig.level.enable = 1;
            calcConfig.level.flag = MVD_CALC_WITH_LEVEL_FLAG_ENABLE_CALC | MVD_CALC_WITH_LEVEL_FLAG_ENABLE_EXTRA_OP |
                                     MVD_CALC_WITH_LEVEL_FLAG_UNK;
            calcConfig.level.level = mvdLevel;
            calcConfig.width = width;
            calcConfig.height = height;
            u32 calculatedSize = 0;
            char line[96];
            std::snprintf(line, sizeof(line), "decode: calling mvdstdCalculateBufferSize levelIdc=%u mvdLevel=%u",
                          spsData[3], mvdLevel);
            log.add(line);
            const Result calcResult = mvdstdCalculateBufferSize(&calcConfig, &calculatedSize);
            std::snprintf(line, sizeof(line), "mvdstdCalculateBufferSize returned 0x%08lx size=%u",
                          static_cast<unsigned long>(calcResult), calculatedSize);
            log.add(line);
            if (R_SUCCEEDED(calcResult) && calculatedSize > 0) {
                workBufSize = calculatedSize;
            } else {
                log.add("decode: calc failed, falling back to MVD_DEFAULT_WORKBUF_SIZE");
            }
        } else {
            char line[64];
            std::snprintf(line, sizeof(line), "decode: unrecognized level_idc=%u, using default workbuf size",
                          spsLen > 3 ? spsData[3] : 0);
            log.add(line);
        }

        if (!completeInit(workBufSize)) {
            log.add("decode: completeInit failed");
            return false;
        }
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
    //
    // All of this message's NALs are concatenated into ONE buffer (each
    // still individually prefixed with its own 00 00 01 start code) and
    // handed to mvdstdProcessVideoFrame() in a SINGLE call, rather than one
    // call per NAL (this function's own previous approach). This matches
    // how Core-2-Extreme/Video_player_for_3DS actually drives MVD: one
    // whole access unit (SPS+PPS+slice for a keyframe, or just a slice for
    // a delta frame) built into one contiguous buffer, processed in one
    // call -- not MVD's own per-NAL API description taken literally.
    {
        char line[80];
        std::snprintf(line, sizeof(line), "decode: len=%zu nalCount=%zu", len, nalRanges.size());
        log.add(line);
    }

    static const uint8_t kStartCode[3] = {0, 0, 1};
    uint8_t *inBytes = static_cast<uint8_t *>(inputBuffer);
    size_t combinedLen = 0;
    int nalIndex = 0;
    for (const auto &[offset, nalLen] : nalRanges) {
        char line[128];
        if (nalLen == 0 || combinedLen + nalLen + 3 > inputBufferCapacity) {
            std::snprintf(line, sizeof(line), "nal[%d]: skipped, nalLen=%zu combinedLen=%zu", nalIndex, nalLen,
                          combinedLen);
            log.add(line);
            nalIndex++;
            continue;
        }
        // NAL header byte (forbidden_zero_bit + nal_ref_idc + nal_unit_type,
        // standard H.264 Annex-B layout) plus the first handful of payload
        // bytes -- to check whether our own Annex-B splitter is actually
        // handing MVD a real, correctly-typed NAL boundary (1=non-IDR
        // slice, 5=IDR slice, 7=SPS, 8=PPS, 6=SEI, 9=AUD, ...) or something
        // that looks like mid-slice garbage, which would point at a
        // splitter bug rather than an MVD/hardware one.
        char hex[32] = { 0 };
        for (size_t i = 0; i < nalLen && i < 8; i++) {
            std::snprintf(hex + i * 3, sizeof(hex) - i * 3, "%02x ", data[offset + i]);
        }
        std::snprintf(line, sizeof(line), "nal[%d]: appending type=%u first=%s", nalIndex, data[offset] & 0x1F, hex);
        log.add(line);
        std::memcpy(inBytes + combinedLen, kStartCode, 3);
        combinedLen += 3;
        std::memcpy(inBytes + combinedLen, data + offset, nalLen);
        combinedLen += nalLen;
        nalIndex++;
    }
    if (combinedLen == 0) {
        log.add("decode: nothing fit in the combined buffer, returning false");
        return false;
    }
    GSPGPU_FlushDataCache(inputBuffer, combinedLen);

    {
        char line[64];
        std::snprintf(line, sizeof(line), "decode: calling mvdstdProcessVideoFrame combinedLen=%zu", combinedLen);
        log.add(line);
    }

    // Poison the output buffer's four corner bytes before processing, same
    // technique Core-2-Extreme/Video_player_for_3DS (a real, working,
    // community-verified MVD user) uses -- mvdstdProcessVideoFrame() and
    // mvdstdRenderVideoFrame()'s own Results are NOT a reliable signal that
    // a picture was actually written: real-world reports confirm they can
    // report success while silently leaving the output buffer untouched
    // (still whatever was there before, i.e. a stale or
    // partially-overwritten previous frame). Trusting the Result alone --
    // what this code did before -- means occasionally uploading exactly
    // that leftover/garbage buffer content as if it were a real decoded
    // frame, which reads as stripes/blocks on screen. Checking whether the
    // corners actually changed is this project's own proven way to tell a
    // real write apart from a no-op success. Checked once right after
    // mvdstdProcessVideoFrame() (a combined multi-NAL buffer can itself
    // complete a picture, same as Core-2-Extreme's own "got_a_frame after
    // mvdstdProcessVideoFrame()" fast path) and, only if still unwritten,
    // again after mvdstdRenderVideoFrame().
    static const uint8_t kPoison = 0x11;
    uint8_t *outBytes = static_cast<uint8_t *>(outputBuffer);
    const size_t lastRowStart = outputBufferSize - static_cast<size_t>(width) * 2;
    outBytes[0] = kPoison;
    outBytes[width * 2 - 1] = kPoison;
    outBytes[lastRowStart] = kPoison;
    outBytes[outputBufferSize - 1] = kPoison;
    GSPGPU_FlushDataCache(outputBuffer, outputBufferSize);

    MVDSTD_ProcessNALUnitOut procOut {};
    const Result processResult =
        mvdstdProcessVideoFrame(inputBuffer, static_cast<u32>(combinedLen), 0, &procOut);
    {
        char line[64];
        std::snprintf(line, sizeof(line), "mvdstdProcessVideoFrame returned 0x%08lx",
                      static_cast<unsigned long>(processResult));
        log.add(line);
    }
    if (!MVD_CHECKNALUPROC_SUCCESS(processResult)) {
        log.add("decode: process failed, returning false");
        return false;
    }
    // MVD_STATUS_PARAMSET: this buffer was just SPS/PPS, no picture data to
    // render yet. MVD_STATUS_INCOMPLETEPROCESSING: doesn't complete a
    // picture on its own -- neither means a frame is ready.
    if (processResult == MVD_STATUS_PARAMSET || processResult == MVD_STATUS_INCOMPLETEPROCESSING) {
        log.add("decode: paramset/incomplete, no frame yet, returning false");
        return false;
    }

    GSPGPU_InvalidateDataCache(outputBuffer, outputBufferSize);
    bool cornersChanged = outBytes[0] != kPoison || outBytes[width * 2 - 1] != kPoison ||
                           outBytes[lastRowStart] != kPoison || outBytes[outputBufferSize - 1] != kPoison;
    {
        char line[48];
        std::snprintf(line, sizeof(line), "decode: cornersChanged after process=%d", cornersChanged ? 1 : 0);
        log.add(line);
    }

    if (!cornersChanged) {
        // Not written yet by mvdstdProcessVideoFrame() alone -- fall
        // through to mvdstdRenderVideoFrame(), same as this function's
        // previous (per-NAL) behavior. Stays a single blocking call
        // (wait=true) rather than Core-2-Extreme's non-blocking
        // mvdstdRenderVideoFrame(NULL, false) retry loop -- see this
        // class's own header comment on why (stock libctru, which this
        // project builds against, rejects a NULL config outright; their
        // retry loop needs a custom libctru fork this project doesn't
        // use). Stock's blocking call already loops internally
        // (MVDSTD_ControlFrameRendering()) until done, so this isn't
        // materially different from their loop, just without a
        // per-iteration sentinel check in between.
        log.add("decode: calling mvdstdRenderVideoFrame");
        const Result renderResult = mvdstdRenderVideoFrame(&config, true);
        {
            char line[64];
            std::snprintf(line, sizeof(line), "mvdstdRenderVideoFrame returned 0x%08lx",
                          static_cast<unsigned long>(renderResult));
            log.add(line);
        }
        if (R_FAILED(renderResult)) {
            log.add("decode: render failed, returning false");
            return false;
        }
        GSPGPU_InvalidateDataCache(outputBuffer, outputBufferSize);
        cornersChanged = outBytes[0] != kPoison || outBytes[width * 2 - 1] != kPoison ||
                          outBytes[lastRowStart] != kPoison || outBytes[outputBufferSize - 1] != kPoison;
        char line[48];
        std::snprintf(line, sizeof(line), "decode: cornersChanged after render=%d", cornersChanged ? 1 : 0);
        log.add(line);
    }

    if (!cornersChanged) {
        log.add("decode: no frame ready, returning false");
        return false;
    }

    log.add("decode: invalidating cache + copying out");
    GSPGPU_InvalidateDataCache(outputBuffer, outputBufferSize);
    outRgb565.resize(outputBufferSize);
    // outputBuffer holds BGR565 pixels (MVD_OUTPUT_BGR565, see the ctor's
    // own comment) -- VideoTex::setFrame() expects RGB565, so this swaps
    // each 16-bit little-endian pixel's 5-bit R/B fields (bits 15-11 and
    // 4-0) while leaving the 6-bit G field (bits 10-5) in the middle
    // untouched, rather than a plain memcpy.
    {
        const uint8_t *src = static_cast<const uint8_t *>(outputBuffer);
        uint8_t *dst = outRgb565.data();
        for (size_t i = 0; i + 1 < outputBufferSize; i += 2) {
            const uint16_t bgr = static_cast<uint16_t>(src[i]) | (static_cast<uint16_t>(src[i + 1]) << 8);
            const uint16_t rgb = static_cast<uint16_t>((bgr & 0x07E0) | ((bgr & 0xF800) >> 11) | ((bgr & 0x001F) << 11));
            dst[i] = static_cast<uint8_t>(rgb & 0xFF);
            dst[i + 1] = static_cast<uint8_t>(rgb >> 8);
        }
    }
    log.add("decode: done, returning true");
    return true;
}
