#include "video_mode_activity.hpp"

#include "prefs.hpp"
#include "strings_generated.hpp"

namespace {
struct VideoModeOption {
    const char *value; // wire-format string, see unison/docs/protocol.md
    const char *label;
};
} // namespace

VideoModeActivity::VideoModeActivity(std::string streamType) : streamType(std::move(streamType)) {}

brls::View *VideoModeActivity::createContentView() {
    auto *column = new brls::Box();
    column->setAxis(brls::Axis::COLUMN);
    column->setAlignItems(brls::AlignItems::STRETCH);
    column->setPadding(24, 32, 24, 32);

    // NOT sorted alphabetically, unlike LanguageActivity's options --
    // deliberate order, per explicit request: the two raw-deflate modes
    // first (legacy/"Raw (Deflate)" before tiles/"Raw+Tiling (Deflate)"),
    // then h264/h265, same as Android's Prefs.VIDEO_MODES.
    VideoModeOption options[] = {
        { "legacy", strings::kVideoModeLegacy },
        { "tiles", strings::kVideoModeTiles },
        { "h264", strings::kVideoModeH264 },
        { "h265", strings::kVideoModeH265 },
    };
    // WIIU_GAMEPAD (Cemu) removed both raw modes entirely (see
    // WiiuGamepadStream.cpp's own SendVideoFrame() comment) -- offering
    // either here would just be a dead choice the server now silently
    // normalizes to h264 anyway (Prefs::videoModeFor()'s own normalizing).
    for (const auto &option : options) {
        if (streamType == "WIIU_GAMEPAD" && (std::string(option.value) == "legacy" || std::string(option.value) == "tiles"))
            continue;
        auto *cell = new brls::DetailCell();
        cell->setText(option.label);
        cell->registerClickAction([this, value = option.value](brls::View *) {
            Prefs prefs;
            prefs.setVideoModeFor(streamType, value);
            prefs.save();
            // ConsoleDetailActivity's own onResume() (called by
            // popActivity() on the activity revealed underneath) reloads
            // Prefs and refreshes its text -- see its own comment
            // (LanguageActivity relies on the exact same mechanism).
            brls::Application::popActivity();
            return true;
        });
        column->addView(cell);
    }

    auto *scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setContentView(column);

    auto *frame = new brls::AppletFrame(scroll);
    frame->setTitle(strings::kSettingsVideoMode);
    return frame;
}
