#pragma once

#include <borealis.hpp>

#include "prefs.hpp"

// Just the video filter toggle -- no on-screen-controls toggle and no
// per-button key rebinding here (unlike clients/android/.../
// SettingsActivity.kt), removed at the user's request since every GBA
// button already has a sensible default physical-controller mapping.
class SettingsActivity : public brls::Activity {
  public:
    brls::View *createContentView() override;

  private:
    Prefs prefs;
    brls::DetailCell *filterCell = nullptr;

    void updateFilterCellUI();
};
