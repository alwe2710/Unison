#pragma once

#include <array>

#include <borealis.hpp>

// Top of the "Konsolenspezifische Einstellungen" cell (SettingsActivity) --
// lists every stream_type docs/protocol.md defines, each a nav cell into
// ConsoleDetailActivity (bilinear-filter toggle + video-mode picker, both
// keyed by that console's own stream_type). Replaces what used to be
// SettingsActivity's own inline per-console filter-toggle list (this
// screen's own predecessor) plus its separate global "Videomodus" cell --
// see ConsoleDetailActivity's own comment. Plain console names, no
// video-mode subtitle (see .cpp) -- so unlike ConsoleDetailActivity, this
// screen holds no Prefs at all; onResume() still exists purely to refresh
// this screen's own text if the language changed one screen up.
class ConsoleSettingsActivity : public brls::Activity {
  public:
    brls::View *createContentView() override;

    void onResume() override;

  private:
    // One row per docs/protocol.md stream_type (kKnownStreamTypes, .cpp).
    std::array<brls::DetailCell *, 4> consoleCells {};
    brls::AppletFrame *frame = nullptr;

    void updateConsoleCellUI(int index);
};
