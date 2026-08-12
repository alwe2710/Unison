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
        MvdLog("ctor: zero dimension, bailing before completeInit");
        initFailed = true;
        return;
    }
    // Eager init with the fixed MVD_DEFAULT_WORKBUF_SIZE constant, same as
    // this class always did before this comment. A real
    // mvdstdCalculateBufferSize()-with-real-level variant (Core-2-Extreme/
    // Video_player_for_3DS's own approach, see completeInit()'s own
    // comment) was tried and reverted: real-hardware logging showed
    // decode() being CALLED but never RETURNING on its very first
    // invocation (no "decode: entered" line ever reached the SD card,
    // which LogBatch only flushes on a normal return -- meaning that call
    // never got that far), immediately followed by the ~10-second
    // connection-timeout symptom this project already diagnosed once
    // before for an unrelated reason (excess synchronous logging). The
    // only genuinely new, never-before-exercised-on-hardware code in that
    // build was the mvdstdCalculateBufferSize() IPC call and the SPS-level
    // parsing feeding it -- the prime suspect for a hang, so it was pulled
    // back out rather than kept alongside the (still unproven either way)
    // combined-NAL-buffer change decode() still uses.
    completeInit(MVD_DEFAULT_WORKBUF_SIZE);
}

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
    //
    // One mvdstdProcessVideoFrame() call per individual NAL -- NOT a
    // combined multi-NAL buffer processed in one call (a since-reverted
    // variant tried that, modeled on Core-2-Extreme/Video_player_for_3DS's
    // own real usage). Real-hardware logging of that variant showed a
    // keyframe message's combined SPS+PPS+SEI+slice buffer coming back
    // MVD_STATUS_INCOMPLETEPROCESSING from a SINGLE process call -- "not
    // all of the input NAL-unit buffer was processed" per that status
    // code's own doc comment -- which this function's own (then-)logic
    // treated as "no frame yet, drop it", silently discarding the IDR
    // slice inside every single keyframe without ever decoding it. Every
    // later delta frame was then being predicted from no valid reference
    // at all, which is what actually produced the persistent
    // gray/blue-drifting stripes report that combined-buffer variant was
    // meant to fix. Per-NAL calls don't hit this: a single already-whole
    // NAL is what mvdstdProcessVideoFrame() actually seems built to fully
    // consume in one call (confirmed in earlier real-hardware logs: a
    // keyframe's own slice NAL, fed alone, returns MVD_STATUS_FRAMEREADY
    // and renders successfully).
    {
        char line[80];
        std::snprintf(line, sizeof(line), "decode: len=%zu nalCount=%zu", len, nalRanges.size());
        log.add(line);
    }

    bool frameReady = false;
    int nalIndex = 0;
    for (const auto &[offset, nalLen] : nalRanges) {
        char line[128];
        if (nalLen == 0 || nalLen + 3 > inputBufferCapacity) {
            std::snprintf(line, sizeof(line), "nal[%d]: skipped, nalLen=%zu", nalIndex, nalLen);
            log.add(line);
            nalIndex++;
            continue;
        }
        static const uint8_t kStartCode[3] = {0, 0, 1};
        std::memcpy(inputBuffer, kStartCode, 3);
        std::memcpy(static_cast<uint8_t *>(inputBuffer) + 3, data + offset, nalLen);
        const size_t bufLen = nalLen + 3;
        GSPGPU_FlushDataCache(inputBuffer, bufLen);

        // NAL header byte (forbidden_zero_bit + nal_ref_idc + nal_unit_type,
        // standard H.264 Annex-B layout) plus the first handful of payload
        // bytes -- to check whether our own Annex-B splitter is actually
        // handing MVD a real, correctly-typed NAL boundary (1=non-IDR
        // slice, 5=IDR slice, 7=SPS, 8=PPS, 6=SEI, 9=AUD, ...) or something
        // that looks like mid-slice garbage, which would point at a
        // splitter bug rather than an MVD/hardware one.
        const uint8_t nalHeader = nalLen > 0 ? data[offset] : 0;
        char hex[32] = { 0 };
        for (size_t i = 0; i < nalLen && i < 8; i++) {
            std::snprintf(hex + i * 3, sizeof(hex) - i * 3, "%02x ", data[offset + i]);
        }
        std::snprintf(line, sizeof(line), "nal[%d]: calling mvdstdProcessVideoFrame bufLen=%zu type=%u first=%s",
                      nalIndex, bufLen, nalHeader & 0x1F, hex);
        log.add(line);
        MVDSTD_ProcessNALUnitOut procOut {};
        const Result processResult = mvdstdProcessVideoFrame(inputBuffer, static_cast<u32>(bufLen), 0, &procOut);
        std::snprintf(line, sizeof(line), "nal[%d]: mvdstdProcessVideoFrame returned 0x%08lx", nalIndex,
                      static_cast<unsigned long>(processResult));
        log.add(line);
        if (!MVD_CHECKNALUPROC_SUCCESS(processResult)) {
            nalIndex++;
            continue;
        }
        // MVD_STATUS_PARAMSET: this NAL was just SPS/PPS, no picture data
        // to render yet. MVD_STATUS_INCOMPLETEPROCESSING: this NAL alone
        // doesn't complete a picture (more slice data needed from a
        // following NAL) -- neither means a frame is ready. Anything else
        // (including the ordinary success case) means this NAL completed
        // a picture, matching the official mvd example's own check.
        if (processResult == MVD_STATUS_PARAMSET || processResult == MVD_STATUS_INCOMPLETEPROCESSING) {
            nalIndex++;
            continue;
        }
        // Poison the output buffer's four corner bytes before rendering,
        // same technique Core-2-Extreme/Video_player_for_3DS (a real,
        // working, community-verified MVD user) uses -- mvdstdRenderVideoFrame()'s
        // own Result is NOT a reliable signal that a picture was actually
        // written: real-world reports confirm it can report success while
        // silently leaving the output buffer untouched (still whatever was
        // there before, i.e. a stale or partially-overwritten previous
        // frame). Trusting the Result alone -- what this code did before --
        // means occasionally uploading exactly that leftover/garbage
        // buffer content as if it were a real decoded frame, which reads
        // as stripes/blocks on screen. Checking whether the corners
        // actually changed is this project's own proven way to tell a real
        // write apart from a no-op success.
        static const uint8_t kPoison = 0x11;
        uint8_t *outBytes = static_cast<uint8_t *>(outputBuffer);
        const size_t lastRowStart = outputBufferSize - static_cast<size_t>(width) * 2;
        outBytes[0] = kPoison;
        outBytes[width * 2 - 1] = kPoison;
        outBytes[lastRowStart] = kPoison;
        outBytes[outputBufferSize - 1] = kPoison;
        GSPGPU_FlushDataCache(outputBuffer, outputBufferSize);

        std::snprintf(line, sizeof(line), "nal[%d]: calling mvdstdRenderVideoFrame", nalIndex);
        log.add(line);
        const Result renderResult = mvdstdRenderVideoFrame(&config, true);
        std::snprintf(line, sizeof(line), "nal[%d]: mvdstdRenderVideoFrame returned 0x%08lx", nalIndex,
                      static_cast<unsigned long>(renderResult));
        log.add(line);
        if (R_FAILED(renderResult)) {
            nalIndex++;
            continue;
        }
        GSPGPU_InvalidateDataCache(outputBuffer, outputBufferSize);
        const bool cornersChanged = outBytes[0] != kPoison || outBytes[width * 2 - 1] != kPoison ||
                                     outBytes[lastRowStart] != kPoison || outBytes[outputBufferSize - 1] != kPoison;
        std::snprintf(line, sizeof(line), "nal[%d]: cornersChanged=%d", nalIndex, cornersChanged ? 1 : 0);
        log.add(line);
        if (!cornersChanged) {
            nalIndex++;
            continue;
        }
        frameReady = true;
        nalIndex++;
    }

    if (!frameReady) {
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
