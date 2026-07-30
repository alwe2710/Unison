#pragma once

#include <borealis.hpp>

// Plain list (System/Deutsch/English), pushed from SettingsActivity's own
// "Sprache"/"Language" cell -- same "tap a row, pick from it, land back
// where you were" flow as every other client's language screen. Picking a
// row applies it and pops straight back to SettingsActivity.
class LanguageActivity : public brls::Activity {
  public:
    brls::View *createContentView() override;
};
