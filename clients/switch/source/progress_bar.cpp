#include "progress_bar.hpp"

#include <algorithm>

ProgressBar::ProgressBar() {
    setHeightPercentage(100);
    setWidthPercentage(100);
    setHeight(6);
}

void ProgressBar::setProgress(float value) {
    progress = std::clamp(value, 0.0f, 1.0f);
}

void ProgressBar::draw(NVGcontext *vg, float x, float y, float width, float height, brls::Style style,
                        brls::FrameContext *ctx) {
    (void)style;
    (void)ctx;

    float r = height / 2.0f;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, width, height, r);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 40));
    nvgFill(vg);

    float filledWidth = width * progress;
    if (filledWidth < height) {
        filledWidth = progress > 0.0f ? height : 0.0f;
    }
    if (filledWidth > 0.0f) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, filledWidth, height, r);
        nvgFillColor(vg, nvgRGB(0, 195, 195));
        nvgFill(vg);
    }
}
