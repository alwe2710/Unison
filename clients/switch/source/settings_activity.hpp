#pragma once

#include <borealis.hpp>

#include "prefs.hpp"

// No on-screen-controls toggle and no per-button key rebinding here (unlike
// clients/android/.../SettingsActivity.kt), removed at the user's request
// since every GBA button already has a sensible default physical-controller
// mapping. Used to also have its own per-console bilinear-filter toggle
// list and a "Videomodus" cell (a single value shared by every console) --
// both moved into ConsoleSettingsActivity's per-console detail screens
// instead (see that class's own comment), so this top level only needs one
// nav cell down to that list now.
class SettingsActivity : public brls::Activity {
  public:
    brls::View *createContentView() override;

    // Called by borealis's popActivity() on the activity revealed
    // underneath -- fires when LanguageActivity (pushed from this screen's
    // own "Sprache"/"Language" cell) pops back here. Reloads Prefs from
    // disk and refreshes every bit of this screen's own text, since
    // borealis keeps a pushed activity's view tree alive rather than
    // recreating it (unlike an Android Activity), so nothing here updates
    // on its own just because the language changed one screen up.
    void onResume() override;

  private:
    Prefs prefs;

    // Navigates to LanguageActivity's plain list on click, current value
    // shown as this cell's detail text (refreshed in onResume() above).
    brls::DetailCell *languageCell = nullptr;
    // Navigates to ConsoleSettingsActivity's 4-console list -- no detail
    // text of its own (there's no single "current" value across all four
    // consoles to summarize here), same as KeyBindingsActivity's own row.
    brls::DetailCell *consoleSettingsCell = nullptr;
    brls::AppletFrame *frame = nullptr;

    void updateLanguageCellUI();
};
