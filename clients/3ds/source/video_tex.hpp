#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include <citro2d.h>

// Video frame (any connected stream type, not just GBA -- see main.cpp's
// discovered-server dispatch) as a citro2d-drawable texture. The PICA200
// GPU only takes power-of-two texture dimensions up to 1024x1024 (its
// hardware limit), so this always allocates a 1024x1024 RGB565 texture
// and only ever draws the top-left frameWidth x frameHeight sub-rectangle
// of it via a Tex3DS_SubTexture. Comfortably fits every stream type this
// app actually connects to today (GBA's 240x160, Cemu's WIIU_GAMEPAD
// 854x480, Azahar's own N3DS_BOTTOM_SCREEN) -- upload() still clamps to
// this size defensively for anything larger (e.g. melonDS's
// NDS_BOTTOM_SCREEN at a high OpenGL scale factor), which crops rather
// than corrupts, but isn't a real target resolution for this client.
// RGB565 is uploaded directly (no RGBA8 conversion, unlike
// clients/switch/source/video_view.cpp's NanoVG path): citro3d's
// GPU_RGB565 texture format matches the wire format exactly, and skipping
// that conversion matters a lot more on the 3DS's much weaker ARM11 CPU
// than on the Switch's.
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

    // Debug only (see main.cpp's "Verbunden" status text) -- the
    // width/height actually decoded into the currently-shown frame, to
    // tell apart "server sent something other than we assumed" from a
    // client-side upload/draw bug without needing a real crash log.
    uint32_t debugFrameWidth() const {
        return frameWidth;
    }
    uint32_t debugFrameHeight() const {
        return frameHeight;
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
    std::vector<uint8_t> staging; // 1024x1024 RGB565 staging buffer for C3D_TexUpload
};
