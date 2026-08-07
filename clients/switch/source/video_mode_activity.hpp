#pragma once

#include <string>

#include <borealis.hpp>

// Plain list (tiles/legacy/h264/h265), pushed from ConsoleDetailActivity's
// own "Videomodus" cell -- same "tap a row, pick from it, land back where
// you were" flow as LanguageActivity, cloned from it wholesale. Picking a
// row sets Prefs::videoModeFor(streamType) and pops straight back.
//
// Per-console (streamType, always constructed by ConsoleDetailActivity now)
// rather than one global choice -- see Prefs::videoModeFor()'s own comment
// on why (picking H.264 for Cemu used to silently also request it from
// Dolphin next time, which never honors it anyway, but still).
class VideoModeActivity : public brls::Activity {
  public:
    explicit VideoModeActivity(std::string streamType);

    brls::View *createContentView() override;

  private:
    std::string streamType;
};
