#pragma once

#include <string>

#include <borealis.hpp>

#include "prefs.hpp"

// Per-console detail screen, reached from ConsoleSettingsActivity's
// 4-console list -- used to be that list itself (one row per stream_type,
// an inline bilinear toggle each and nothing else, see
// SettingsActivity.cpp's git history); moved out so this screen could show
// more than a single toggle per console without cramming both the
// antialiasing switch and a video-mode picker into one flat list row. Two
// settings live here, both keyed by streamType: bilinear-vs-nearest upscale
// (unchanged from before -- PlayerActivity reads whatever's configured here
// via Prefs::bilinearFor()) and the video-mode/compression picker (own
// sub-screen, VideoModeActivity) -- used to be a single global choice
// shared by every console (SettingsActivity's own former "Videomodus"
// cell), moved here per console for the same reason antialiasing already
// was.
class ConsoleDetailActivity : public brls::Activity {
  public:
    ConsoleDetailActivity(std::string streamType, std::string label);

    brls::View *createContentView() override;

    // Fires when VideoModeActivity pops back here -- same mechanism
    // SettingsActivity's own onResume() used for its old "Videomodus" cell.
    void onResume() override;

  private:
    std::string streamType;
    std::string label;
    Prefs prefs;

    brls::DetailCell *bilinearCell = nullptr;
    brls::DetailCell *videoModeCell = nullptr;
    brls::AppletFrame *frame = nullptr;

    void updateBilinearCellUI();
    void updateVideoModeCellUI();
};
