#pragma once

#include <borealis.hpp>

// A plain determinate linear progress bar (filled track), matching the
// Android client's discovery progress indicator. borealis's own
// ProgressSpinner is an indeterminate spinner, not this.
class ProgressBar : public brls::View {
  public:
    ProgressBar();

    void draw(NVGcontext *vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext *ctx) override;

    void setProgress(float value); // 0.0 - 1.0

  private:
    float progress = 0.0f;
};
