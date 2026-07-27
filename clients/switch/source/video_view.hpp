#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include <borealis.hpp>

// Fullscreen GBA video: renders the latest decoded frame (scaled to fit,
// aspect-preserved, like the Android client's ContentScale.Fit) via a
// NanoVG image pattern. No on-screen touch overlay here (unlike
// clients/android/.../PlayerActivity.kt) -- removed at the user's
// request, since every GBA button already has a sensible default
// physical-controller mapping (see gba_buttons.hpp).
class VideoView : public brls::View {
  public:
    VideoView();
    ~VideoView() override;

    void draw(NVGcontext *vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext *ctx) override;

    // Called from the session's background thread; only stores the frame,
    // the actual GL/NanoVG upload happens on the render thread inside draw().
    void setFrame(uint32_t width, uint32_t height, const std::vector<uint8_t> &rgb565);

    void setBilinearFilter(bool bilinear);

  private:
    std::mutex frameMutex;
    uint32_t pendingWidth = 0, pendingHeight = 0;
    std::vector<uint8_t> pendingRgba;
    bool frameDirty = false;

    int imageId = -1;
    int imageWidth = 0, imageHeight = 0;
    bool imageBilinear = false;
    bool wantBilinear = false;
};
