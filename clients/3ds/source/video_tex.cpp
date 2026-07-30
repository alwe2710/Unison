#include "video_tex.hpp"

#include <algorithm>
#include <cstring>

namespace {
constexpr int kTexSize = 1024;
}

VideoTex::VideoTex() {
    texInited = C3D_TexInit(&tex, kTexSize, kTexSize, GPU_RGB565);
    if (texInited) {
        C3D_TexSetFilter(&tex, GPU_NEAREST, GPU_NEAREST);
        staging.assign(static_cast<size_t>(kTexSize) * kTexSize * 2, 0);
    }
}

VideoTex::~VideoTex() {
    if (texInited) {
        C3D_TexDelete(&tex);
    }
}

void VideoTex::setFrame(uint32_t width, uint32_t height, const std::vector<uint8_t> &rgb565) {
    std::lock_guard<std::mutex> lock(frameMutex);
    pendingWidth = width;
    pendingHeight = height;
    pendingRgb565 = rgb565;
    frameDirty = true;
}

void VideoTex::setBilinearFilter(bool value) {
    if (bilinear == value || !texInited) {
        bilinear = value;
        return;
    }
    bilinear = value;
    GPU_TEXTURE_FILTER_PARAM filter = bilinear ? GPU_LINEAR : GPU_NEAREST;
    C3D_TexSetFilter(&tex, filter, filter);
}

void VideoTex::upload() {
    if (!texInited) {
        return;
    }
    std::lock_guard<std::mutex> lock(frameMutex);
    if (!frameDirty || pendingRgb565.empty()) {
        return;
    }

    uint32_t w = std::min<uint32_t>(pendingWidth, kTexSize);
    uint32_t h = std::min<uint32_t>(pendingHeight, kTexSize);
    for (uint32_t row = 0; row < h; row++) {
        std::memcpy(&staging[static_cast<size_t>(row) * kTexSize * 2], &pendingRgb565[static_cast<size_t>(row) * pendingWidth * 2],
                    static_cast<size_t>(w) * 2);
    }
    C3D_TexUpload(&tex, staging.data());

    frameWidth = w;
    frameHeight = h;
    frameDirty = false;
}

void VideoTex::reset() {
    std::lock_guard<std::mutex> lock(frameMutex);
    frameWidth = 0;
    frameHeight = 0;
    frameDirty = false;
    pendingRgb565.clear();
}

void VideoTex::drawFitted(float x, float y, float w, float h) const {
    if (!texInited || frameWidth == 0 || frameHeight == 0) {
        return;
    }

    Tex3DS_SubTexture sub;
    sub.width = static_cast<u16>(frameWidth);
    sub.height = static_cast<u16>(frameHeight);
    sub.left = 0.0f;
    sub.top = 1.0f;
    sub.right = static_cast<float>(frameWidth) / kTexSize;
    sub.bottom = 1.0f - static_cast<float>(frameHeight) / kTexSize;

    C2D_Image img { const_cast<C3D_Tex *>(&tex), &sub };

    float scale = std::min(w / frameWidth, h / frameHeight);
    float dw = frameWidth * scale, dh = frameHeight * scale;
    float dx = x + (w - dw) / 2.0f, dy = y + (h - dh) / 2.0f;

    C2D_DrawImageAt(img, dx, dy, 0.5f, nullptr, scale, scale);
}
