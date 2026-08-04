#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <borealis.hpp>
#include <switch.h>

#include "prefs.hpp"
#include "session.hpp"
#include "video_view.hpp"

// The actual stream view: connects to host:port (passed in via the
// constructor, set by MenuActivity), shows video full-screen, plays
// audio, and reads input from the physical controller using each GBA
// button's default mapping (see gba_buttons.hpp) -- no on-screen touch
// overlay and no per-button rebinding, removed at the user's request.
// Owns the one GbaSession instance for its lifetime -- Menu and Settings
// never touch it. Mirrors clients/android/.../PlayerActivity.kt.
//
// Not wrapped in an AppletFrame: B is a GBA input here (mapped to the GBA
// B button by default), not "go back" like every other screen -- unlike
// Android, where the system back button/gesture is a channel entirely
// separate from any of the app's own key handling, every one of the
// Switch's face/shoulder/dpad buttons is claimed by the GBA button
// mapping. Exiting instead requires holding ZL+ZR (BUTTON_LT+BUTTON_RT,
// the triggers -- distinct from the L/R bumpers used for GBA L/R), shown
// as an on-screen hint.
class PlayerActivity : public brls::Activity {
  public:
    // streamType is whatever MenuActivity already knew before launching
    // this activity (the discovered beacon's stream_type, or
    // kStreamTypeGcGbaLink for the P1-P4 picker/manual-IP paths, which are
    // GC_GBA_LINK-only by construction) -- used only to look up this
    // connection's antialiasing preference (Prefs::bilinearFor(), set from
    // SettingsActivity's per-stream-type list), not sent anywhere.
    PlayerActivity(std::string host, int port, std::string streamType);
    ~PlayerActivity() override;

    brls::View *createContentView() override;

  private:
    std::string host;
    int port;
    std::string streamType;

    Prefs prefs;
    GbaSession session;
    VideoView *videoView = nullptr;
    brls::Label *statusLabel = nullptr;

    bool connected = false;
    uint16_t physicalMask = 0;
    float exitHoldSeconds = 0.0f;
    // Application::popActivity() only actually removes this activity once
    // its hide animation finishes (a callback, not immediate) -- until
    // then it's still in the stack and this activity's own FramePoller
    // keeps ticking, so without this guard, holding ZL+ZR past the
    // threshold called popActivity() again on every subsequent frame,
    // popping the focus stack an extra time each call and leaving focus
    // on the wrong view once back on the Menu.
    bool exiting = false;

    bool audioOpen = false;

    void onFrameTick();
    void sendCombinedInput();
    void playAudio(uint32_t sampleRate, uint8_t channels, std::vector<int16_t> pcm);
    void reclaimAudioBuffers();
    void closeAudio();
    void showDisconnectDialog(const std::string &reason);
    void showVideoModeFallbackDialog(const std::string &requested, const std::string &granted);
};
