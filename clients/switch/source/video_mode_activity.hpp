#pragma once

#include <borealis.hpp>

// Plain list (tiles/legacy/h264/h265), pushed from SettingsActivity's own
// "Videomodus" cell -- same "tap a row, pick from it, land back where you
// were" flow as LanguageActivity, cloned from it wholesale. Picking a row
// sets Prefs::videoMode and pops straight back to SettingsActivity.
class VideoModeActivity : public brls::Activity {
  public:
    brls::View *createContentView() override;
};
