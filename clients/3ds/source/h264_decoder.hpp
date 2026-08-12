#pragma once

#include <cstdint>
#include <vector>

#include <3ds.h>

// Hardware H.264 decoder via the New3DS-exclusive MVD (Movie/Video
// Decoder) service -- there is deliberately no H.265 counterpart on this
// client (see session.hpp's Listener::onCompressedVideoFrame comment): MVD
// only ever supported H.264 (the 3DS predates HEVC entirely), and software
// HEVC decode on the 3DS's ARM11 CPU isn't remotely practical.
//
// Old 3DS/2DS has no MVD hardware at all -- mvdstdInit() simply fails there
// (the service isn't registered), which this class surfaces as isValid()
// == false rather than distinguishing "no hardware" from any other real
// init failure; callers should fall back the same way either way (drop
// h264 frames, same "unsupported format" spirit as every other client's
// own genuinely-undecodable-message handling).
//
// API shape (mvdstdInit/mvdstdGenerateDefaultConfig/mvdstdProcessVideoFrame/
// mvdstdRenderVideoFrame) matches libctru's own devkitPro/3ds-examples
// "mvd" example -- ported to this class's own per-message decode() call
// shape (one Annex-B blob in, one RGB565 picture out or none yet) rather
// than that example's whole-file-up-front batch-decode loop.
//
// Real MVD usage was cross-checked against Core-2-Extreme/
// Video_player_for_3DS (an actively maintained, real-world working player,
// github.com/Core-2-Extreme/Video_player_for_3DS,
// source/system/util/decoder.c). Adopted from it: the corner-poison
// sentinel technique (decode()'s own comment). Tried and reverted, both
// found guilty by real-hardware logging:
//   - Its mvdstdCalculateBufferSize()-with-real-level work-buffer sizing
//     (needs init moved out of the constructor into decode()'s first
//     call): decode() was observed being called but never returning on
//     its very first invocation (no trace of it reaching even its own
//     first log line), immediately followed by the same ~10-second
//     connection-timeout symptom this project once diagnosed for an
//     unrelated reason (see LogBatch's own comment). Back to eager
//     construction-time init with the fixed MVD_DEFAULT_WORKBUF_SIZE
//     constant (completeInit()'s own comment).
//   - Its pattern of concatenating a whole access unit's NALs into one
//     buffer for a single mvdstdProcessVideoFrame() call: a keyframe
//     message's combined SPS+PPS+SEI+slice buffer came back
//     MVD_STATUS_INCOMPLETEPROCESSING from ONE such call, which this
//     function's logic (correctly, for the per-NAL case) treats as "no
//     frame yet" and drops -- silently discarding the IDR slice inside
//     every keyframe, every time, without ever decoding it. Back to one
//     mvdstdProcessVideoFrame() call per individual NAL (decode()'s own
//     comment), which real-hardware logs confirm MVD actually fully
//     consumes in one call.
// Also NOT adopted: their non-blocking mvdstdRenderVideoFrame(NULL, false)
// retry loop, which its own comment says needs a custom libctru fork
// (github.com/Core-2-Extreme/libctru_custom) -- stock libctru (what this
// project builds against) returns an immediate error for a NULL config
// (confirmed by reading libctru's own mvd.c), so this class keeps the
// original blocking mvdstdRenderVideoFrame(&config, true) call, which
// already loops internally inside stock libctru until done.
class H264Decoder {
  public:
    // inputWidth/inputHeight are the encoder's *coded* dimensions (see
    // SoftwareVideoEncoder::CodedWidth()'s own comment on the host repos)
    // -- MVD's own input/output config is fixed for this decoder's whole
    // lifetime, unlike a session whose negotiated size could differ on a
    // later reconnect; the caller (session.cpp's onCompressedVideoFrame
    // handler) is expected to construct a fresh H264Decoder if that ever
    // happens, same as every sibling client's own lazy-build-on-first-use
    // pattern.
    H264Decoder(uint32_t inputWidth, uint32_t inputHeight);
    ~H264Decoder();

    H264Decoder(const H264Decoder &) = delete;
    H264Decoder &operator=(const H264Decoder &) = delete;

    // True if mvdstdInit()+buffer allocation succeeded -- see this class's
    // own top comment on why a false here doesn't distinguish "no MVD
    // hardware" (Old 3DS/2DS) from any other init failure.
    bool isValid() const {
        return initialized;
    }

    // data/len is one message's whole compressed_data blob (Annex-B, one
    // or more NAL units -- a keyframe message also carries SPS/PPS ahead
    // of its slice data). Decodes whatever picture that produces into
    // outRgb565 (resized to inputWidth*inputHeight*2 bytes on success) --
    // returns false whenever there's nothing new to display yet (a
    // parameter-set-only NAL, a NAL that doesn't complete a picture on its
    // own, or a real decode error all look the same to the caller: skip
    // this message, the next forced keyframe self-heals anything that
    // matters, docs/protocol.md's "Keyframe discipline").
    bool decode(const uint8_t *data, size_t len, std::vector<uint8_t> &outRgb565);

  private:
    bool initialized = false;
    // Set if completeInit() itself fails (buffer alloc, mvdstdInit) or the
    // constructor bails on a zero dimension -- kept as a separate flag
    // (rather than just checking !initialized) as a leftover hook from a
    // since-reverted lazy-init variant of this class; harmless to keep,
    // isValid() still just checks initialized.
    bool initFailed = false;
    uint32_t width = 0, height = 0;
    MVDSTD_Config config {};

    // Does the real MVD init (mvdstdInit), buffer allocation, and
    // mvdstdGenerateDefaultConfig() -- called once from the constructor
    // with the fixed MVD_DEFAULT_WORKBUF_SIZE constant. Factored out of
    // the constructor only because a since-reverted variant needed to call
    // this lazily from decode() instead (see this class's own top
    // comment); kept as a separate function since there's no longer any
    // reason to inline it back into the constructor.
    bool completeInit(uint32_t workBufSize);

    // Linear (DMA-visible) memory MVD's hardware reads/writes through
    // directly -- CPU cache must be explicitly flushed (before the
    // hardware reads what the CPU just wrote) / invalidated (before the
    // CPU reads what the hardware just wrote) around every use, unlike
    // ordinary heap memory the CPU's own cache coherency already handles
    // transparently.
    void *inputBuffer = nullptr;
    size_t inputBufferCapacity = 0;
    void *outputBuffer = nullptr;
    size_t outputBufferSize = 0;
};
