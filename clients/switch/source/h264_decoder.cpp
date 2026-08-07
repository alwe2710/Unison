#include "h264_decoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

H264Decoder::H264Decoder(bool isH265) {
    const AVCodec *codec = avcodec_find_decoder(isH265 ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
    if (!codec) {
        return;
    }
    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext) {
        return;
    }
    if (avcodec_open2(codecContext, codec, nullptr) < 0) {
        avcodec_free_context(&codecContext);
        codecContext = nullptr;
        return;
    }
    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (!packet || !frame) {
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&codecContext);
        codecContext = nullptr;
    }
}

H264Decoder::~H264Decoder() {
    if (swsContext) {
        sws_freeContext(swsContext);
    }
    av_frame_free(&frame);
    av_packet_free(&packet);
    if (codecContext) {
        avcodec_free_context(&codecContext);
    }
}

bool H264Decoder::decode(const uint8_t *data, size_t len, std::vector<uint8_t> &outRgba, uint32_t &outWidth,
                          uint32_t &outHeight) {
    if (!codecContext || len == 0) {
        return false;
    }

    // Annex-B straight into the packet -- avcodec_send_packet() for the
    // H.264/HEVC decoders accepts Annex-B directly (unlike a muxed
    // container needing a bitstream filter first), matching x264/x265's
    // own b_annexb=1 output on every host repo's SoftwareVideoEncoder.
    // const_cast is safe: avcodec_send_packet() never writes through this
    // pointer, AVPacket::data is just non-const for the (unrelated) case
    // of an owned/refcounted packet.
    packet->data = const_cast<uint8_t *>(data);
    packet->size = static_cast<int>(len);

    const int sendResult = avcodec_send_packet(codecContext, packet);
    if (sendResult < 0 && sendResult != AVERROR(EAGAIN)) {
        return false;
    }

    const int receiveResult = avcodec_receive_frame(codecContext, frame);
    if (receiveResult < 0) {
        // AVERROR(EAGAIN) (decoder needs more input before it can produce
        // a picture -- internal look-ahead/buffering) is the overwhelmingly
        // common case here, not a real error -- see this function's own
        // header comment on why both are treated identically by callers.
        return false;
    }
    if (frame->width <= 0 || frame->height <= 0) {
        av_frame_unref(frame);
        return false;
    }

    if (!swsContext || swsWidth != frame->width || swsHeight != frame->height) {
        if (swsContext) {
            sws_freeContext(swsContext);
        }
        swsContext = sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                                    frame->width, frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr,
                                    nullptr);
        swsWidth = frame->width;
        swsHeight = frame->height;
    }
    if (!swsContext) {
        av_frame_unref(frame);
        return false;
    }

    outWidth = static_cast<uint32_t>(frame->width);
    outHeight = static_cast<uint32_t>(frame->height);
    outRgba.resize(static_cast<size_t>(outWidth) * outHeight * 4);

    uint8_t *dstData[4] = { outRgba.data(), nullptr, nullptr, nullptr };
    int dstLinesize[4] = { static_cast<int>(outWidth) * 4, 0, 0, 0 };
    sws_scale(swsContext, frame->data, frame->linesize, 0, frame->height, dstData, dstLinesize);

    av_frame_unref(frame);
    return true;
}
