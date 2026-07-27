#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include <citro2d.h>

// GBA video frame as a citro2d-drawable texture. The PICA200 GPU only
// takes power-of-two texture dimensions, so this always allocates a
// 256x256 RGB565 texture and only ever draws the top-left
// frameWidth x frameHeight sub-rectangle of it via a Tex3DS_SubTexture --
// the GBA's native 240x160 comfortably fits. RGB565 is uploaded directly
// (no RGBA8 conversion, unlike clients/switch/source/video_view.cpp's
// NanoVG path): citro3d's GPU_RGB565 texture format matches the wire
// format exactly, and skipping that conversion matters a lot more on the
// 3DS's much weaker ARM11 CPU than on the Switch's.
class VideoTex {
  public:
    VideoTex();
    ~VideoTex();

    // Called from the session's background thread; only stores the frame.
    // The actual GPU texture upload happens on the main thread inside
    // upload(), which must be called once per frame before draw().
    void setFrame(uint32_t width, uint32_t height, const std::vector<uint8_t> &rgb565);

    void setBilinearFilter(bool bilinear);

    // Uploads the latest pending frame to the GPU, if any. Must be called
    // from the main/render thread, outside of C3D_FrameBegin/End.
    void upload();

    // Clears the currently-displayed frame (hasFrame() goes back to
    // false) and drops any not-yet-uploaded pending one. Unlike the
    // Android/Switch clients, where a fresh VideoTex-equivalent object is
    // created for every new connection, this one is a single instance
    // shared across the app's whole lifetime (see main.cpp) -- without
    // this, reconnecting to a different host/session would keep showing
    // the previous stream's last frame during the "Verbinde..." gap
    // before the new stream's first (always full, per docs/protocol.md)
    // frame arrives.
    void reset();

    bool hasFrame() const {
        return frameWidth > 0 && frameHeight > 0;
    }

    // Draws scaled to fit within (x,y,w,h), aspect-preserved and centered,
    // like the other clients' ContentScale.Fit-equivalent behavior.
    void drawFitted(float x, float y, float w, float h) const;

  private:
    C3D_Tex tex {};
    bool texInited = false;
    bool bilinear = false;

    std::mutex frameMutex;
    std::vector<uint8_t> pendingRgb565;
    uint32_t pendingWidth = 0, pendingHeight = 0;
    bool frameDirty = false;

    uint32_t frameWidth = 0, frameHeight = 0;
    std::vector<uint8_t> staging; // 256x256 RGB565 staging buffer for C3D_TexUpload
};
