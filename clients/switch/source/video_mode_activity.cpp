#include "video_mode_activity.hpp"

#include "prefs.hpp"
#include "strings_generated.hpp"

namespace {
struct VideoModeOption {
    const char *value; // wire-format string, see finlink/docs/protocol.md
    const char *label;
};
} // namespace

brls::View *VideoModeActivity::createContentView() {
    auto *column = new brls::Box();
    column->setAxis(brls::Axis::COLUMN);
    column->setAlignItems(brls::AlignItems::STRETCH);
    column->setPadding(24, 32, 24, 32);

    // NOT sorted alphabetically, unlike LanguageActivity's options --
    // deliberate order (default first, then the two stronger/lossier
    // compressed options, legacy last as the explicit-opt-out fallback),
    // same as Android's Prefs.VIDEO_MODES.
    VideoModeOption options[] = {
        { "tiles", strings::kVideoModeTiles },
        { "h264", strings::kVideoModeH264 },
        { "h265", strings::kVideoModeH265 },
        { "legacy", strings::kVideoModeLegacy },
    };
    for (const auto &option : options) {
        auto *cell = new brls::DetailCell();
        cell->setText(option.label);
        cell->registerClickAction([value = option.value](brls::View *) {
            Prefs prefs;
            prefs.videoMode = value;
            prefs.save();
            // SettingsActivity's own onResume() (called by popActivity() on
            // the activity revealed underneath) reloads Prefs and refreshes
            // its text -- see its own comment (LanguageActivity relies on
            // the exact same mechanism).
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
