#pragma once

#include <cstdint>
#include <vector>

// Forward-declared, not #include <libavcodec/avcodec.h> etc. here -- keeps
// FFmpeg's headers out of every includer of this file (same reasoning as
// SoftwareVideoEncoder's own opaque void* encoder handle on the host
// repos); h264_decoder.cpp is the only translation unit that needs the
// real types.
extern "C" {
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct SwsContext;
}

// Software H.264/H265 decoder (libavcodec, via the switch-ffmpeg devkitPro
// package -- pkg-config style: pacman -S switch-ffmpeg) for a compressed
// UNISON_VIDEO_FORMAT_H264/_H265 session. Mirrors the sibling clients' own
// compressed-video decode path (Android's MediaCodec, iOS's
// CompressedVideoDecoder/VideoToolbox): decodes one Annex-B NAL blob (one
// UNISON_MSG_VIDEO message's compressed_data) at a time into an RGBA8
// buffer ready for VideoView::setFrameRGBA().
//
// Software, not NVDEC hardware decode: there's no documented, ready-made
// libnx wrapper for Switch homebrew's NVDEC access at the time this was
// written (real Switch homebrew streaming clients that do use it, e.g.
// SwitchNOW, do so via a heavily patched custom FFmpeg fork -- averne's
// NVTEGRA branch -- not a small self-contained API this project can just
// call into). Plain libavcodec software decode is what's actually
// available as a normal, documented devkitPro package and verifiable to
// link; a real hardware-decode path is a possible future upgrade, not
// something this class rules out (H264Decoder's own interface doesn't
// assume software decode specifically, only VideoView::setFrameRGBA()'s
// "here are RGBA8 pixels" contract).
class H264Decoder {
  public:
    // isH265 picks AV_CODEC_ID_HEVC vs. AV_CODEC_ID_H264 -- fixed for this
    // decoder's whole lifetime, matching how a session negotiates exactly
    // one video_mode for its whole duration (same convention as the
    // sibling host repos' own SoftwareVideoEncoder, and iOS's
    // CompressedVideoDecoder).
    explicit H264Decoder(bool isH265);
    ~H264Decoder();

    H264Decoder(const H264Decoder &) = delete;
    H264Decoder &operator=(const H264Decoder &) = delete;

    // True if the decoder opened successfully -- check before calling
    // decode(); a construction failure (e.g. the codec genuinely isn't
    // available in this switch-ffmpeg build) should make the caller treat
    // every frame as undecodable rather than crash.
    bool isValid() const { return codecContext != nullptr; }

    // data/len is one message's whole compressed_data blob (Annex-B, one
    // or more NAL units -- a keyframe message also carries SPS/PPS ahead
    // of its slice data, same layout every encoder here produces).
    // Decodes whatever picture that produces into outRgba (resized as
    // needed) and outWidth/outHeight. Returns false whenever there's
    // nothing new to actually display -- either a real decode error, or
    // (just as commonly, not an error at all) the decoder's own internal
    // look-ahead/buffering not having produced a picture yet for this
    // particular input -- callers should treat both the same way every
    // other client's own compressed-video path does: skip this message,
    // the next forced keyframe self-heals anything that matters (docs/
    // protocol.md's "Keyframe discipline").
    bool decode(const uint8_t *data, size_t len, std::vector<uint8_t> &outRgba, uint32_t &outWidth,
                uint32_t &outHeight);

  private:
    AVCodecContext *codecContext = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    // Rebuilt (in decode()) whenever the decoded frame's own width/height
    // changes -- sws_getContext() is fixed to one input/output size for
    // its whole lifetime, unlike a decoder that can legitimately start
    // producing a different picture size mid-stream (a differently-
    // negotiated reconnect against the same host, or -- for Cemu's own
    // WIIU_GAMEPAD -- the DRC content resolution genuinely changing
    // mid-session, see SoftwareVideoEncoder::Width()'s own comment on that
    // host repo).
    SwsContext *swsContext = nullptr;
    int swsWidth = 0, swsHeight = 0;
};
