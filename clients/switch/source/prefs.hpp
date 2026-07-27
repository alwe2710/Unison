#pragma once

#include <map>
#include <string>

// Settings, read/written from SettingsActivity and read by PlayerActivity.
// Persisted as a flat key=value text file on the SD card -- there's no
// SharedPreferences equivalent in libnx, and the one setting left here
// doesn't warrant pulling in a real config/JSON library.
//
// No on-screen-touch-overlay toggle and no per-GBA-button key rebinding
// here (unlike clients/android/.../Prefs.kt) -- both were removed at the
// user's request: every GBA button already has a sensible default
// physical-controller mapping (see gba_buttons.hpp), so neither concept
// pulled its weight on this platform.
class Prefs {
  public:
    Prefs();

    void save();

    // true = bilinear filtering (smooth upscale), false = nearest-neighbor
    // (crisp/pixelated upscale, the default) -- same rationale as the
    // Android client's toggle: the GBA's native 240x160 framebuffer is
    // upscaled a lot to fill the screen.
    bool bilinearVideoFilter = false;

  private:
    std::map<std::string, std::string> values;

    void load();
    std::string path() const;
};
