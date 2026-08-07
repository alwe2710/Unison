#include "video_view.hpp"

#include <algorithm>

VideoView::VideoView() {
    this->setWidthPercentage(100);
    this->setHeightPercentage(100);
}

VideoView::~VideoView() {
    // Image deletion needs a live NVGcontext, which we don't keep a
    // reference to outside of draw() -- leaking the GL texture here is
    // fine, the process/console reclaims it on exit, and the player is
    // only ever created/destroyed a handful of times per session.
}

void VideoView::setFrame(uint32_t width, uint32_t height, const std::vector<uint8_t> &rgb565) {
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
    for (size_t i = 0; i < static_cast<size_t>(width) * height; i++) {
        uint16_t px = static_cast<uint16_t>(rgb565[i * 2] | (rgb565[i * 2 + 1] << 8));
        uint8_t r5 = (px >> 11) & 0x1F;
        uint8_t g6 = (px >> 5) & 0x3F;
        uint8_t b5 = px & 0x1F;
        rgba[i * 4 + 0] = static_cast<uint8_t>((r5 * 255 + 15) / 31);
        rgba[i * 4 + 1] = static_cast<uint8_t>((g6 * 255 + 31) / 63);
        rgba[i * 4 + 2] = static_cast<uint8_t>((b5 * 255 + 15) / 31);
        rgba[i * 4 + 3] = 255;
    }

    setFrameRGBA(width, height, rgba);
}

void VideoView::setFrameRGBA(uint32_t width, uint32_t height, const std::vector<uint8_t> &rgba) {
    std::lock_guard<std::mutex> lock(frameMutex);
    pendingWidth = width;
    pendingHeight = height;
    pendingRgba = rgba;
    frameDirty = true;
}

void VideoView::setBilinearFilter(bool bilinear) {
    wantBilinear = bilinear;
    std::lock_guard<std::mutex> lock(frameMutex);
    frameDirty = frameDirty || (imageId >= 0 && imageBilinear != bilinear);
}

void VideoView::draw(NVGcontext *vg, float x, float y, float width, float height, brls::Style style,
                      brls::FrameContext *ctx) {
    (void)style;
    (void)ctx;

    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, nvgRGB(0, 0, 0));
    nvgFill(vg);

    {
        std::lock_guard<std::mutex> lock(frameMutex);
        if (frameDirty && !pendingRgba.empty()) {
            int flags = wantBilinear ? 0 : NVG_IMAGE_NEAREST;
            if (imageId < 0 || imageWidth != static_cast<int>(pendingWidth) ||
                imageHeight != static_cast<int>(pendingHeight) || imageBilinear != wantBilinear) {
                if (imageId >= 0) {
                    nvgDeleteImage(vg, imageId);
                }
                imageId = nvgCreateImageRGBA(vg, static_cast<int>(pendingWidth), static_cast<int>(pendingHeight),
                                              flags, pendingRgba.data());
                imageWidth = static_cast<int>(pendingWidth);
                imageHeight = static_cast<int>(pendingHeight);
                imageBilinear = wantBilinear;
            } else {
                nvgUpdateImage(vg, imageId, pendingRgba.data());
            }
            frameDirty = false;
        }
    }

    if (imageId >= 0 && imageWidth > 0 && imageHeight > 0) {
        float scale = std::min(width / imageWidth, height / imageHeight);
        float dw = imageWidth * scale, dh = imageHeight * scale;
        float dx = x + (width - dw) / 2, dy = y + (height - dh) / 2;

        NVGpaint paint = nvgImagePattern(vg, dx, dy, dw, dh, 0, imageId, 1.0f);
        nvgBeginPath(vg);
        nvgRect(vg, dx, dy, dw, dh);
        nvgFillPaint(vg, paint);
        nvgFill(vg);
    }
}
