#pragma once

#include <array>

#include <borealis.hpp>

#include "prefs.hpp"

// Just the per-console video filter toggles -- no on-screen-controls
// toggle and no per-button key rebinding here (unlike clients/android/.../
// SettingsActivity.kt), removed at the user's request since every GBA
// button already has a sensible default physical-controller mapping.
class SettingsActivity : public brls::Activity {
  public:
    brls::View *createContentView() override;

  private:
    Prefs prefs;
    // One row per docs/protocol.md stream_type (kKnownStreamTypes, .cpp) --
    // antialiasing is a per-console preference (Prefs::bilinearFor()), not
    // one global toggle, since GBA/DS pixel art and a Wii U GamePad's
    // higher-effective-resolution render suit different filtering. This
    // screen has no notion of "the current" stream_type (PlayerActivity,
    // which does, reads whatever was configured here in advance instead --
    // see its own constructor), so every known type gets its own row.
    std::array<brls::DetailCell *, 4> filterCells {};

    void updateFilterCellUI(int index);
};
