#include "console_detail_activity.hpp"

#include "strings_generated.hpp"
#include "video_mode_activity.hpp"

namespace {
// Same on/off color convention as the Switch's own system settings: gray
// for off, turquoise for on -- text-based ("Ein"/"Aus"), not a graphical
// switch (the custom-drawn ToggleSwitch this replaced didn't reliably
// register clicks; DetailCell is the same proven, already-working
// clickable/focusable row used everywhere else in Menu/Settings). Mirrors
// the colors SettingsActivity's own former per-console list used.
constexpr NVGcolor kColorOff = { { { 150.0f / 255.0f, 150.0f / 255.0f, 150.0f / 255.0f, 1.0f } } };
constexpr NVGcolor kColorOn = { { { 0.0f / 255.0f, 195.0f / 255.0f, 195.0f / 255.0f, 1.0f } } };

// Wire-format string (unison/docs/protocol.md) -> label. Falls back to
// "tiles" for anything unrecognized, same as a server would.
const char *labelForVideoMode(const std::string &mode) {
    if (mode == "h264") return strings::kVideoModeH264;
    if (mode == "h265") return strings::kVideoModeH265;
    if (mode == "legacy") return strings::kVideoModeLegacy;
    return strings::kVideoModeTiles;
}
} // namespace

ConsoleDetailActivity::ConsoleDetailActivity(std::string streamType, std::string label)
    : streamType(std::move(streamType)), label(std::move(label)) {}

void ConsoleDetailActivity::updateBilinearCellUI() {
    bool on = prefs.bilinearFor(streamType);
    bilinearCell->setDetailText(on ? strings::kFilterOn : strings::kFilterOff);
    bilinearCell->setDetailTextColor(on ? kColorOn : kColorOff);
}

void ConsoleDetailActivity::updateVideoModeCellUI() {
    videoModeCell->setDetailText(labelForVideoMode(prefs.videoModeFor(streamType)));
}

void ConsoleDetailActivity::onResume() {
    brls::Activity::onResume();
    prefs = Prefs();
    frame->setTitle(label.c_str());
    bilinearCell->setText(strings::kSettingsAntialiasing);
    videoModeCell->setText(strings::kSettingsVideoMode);
    updateBilinearCellUI();
    updateVideoModeCellUI();
}

brls::View *ConsoleDetailActivity::createContentView() {
    auto *column = new brls::Box();
    column->setAxis(brls::Axis::COLUMN);
    column->setAlignItems(brls::AlignItems::STRETCH);
    column->setPadding(24, 32, 24, 32);

    bilinearCell = new brls::DetailCell();
    bilinearCell->setText(strings::kSettingsAntialiasing);
    bilinearCell->registerClickAction([this](brls::View *) {
        prefs.setBilinearFor(streamType, !prefs.bilinearFor(streamType));
        prefs.save();
        updateBilinearCellUI();
        return true;
    });
    updateBilinearCellUI();
    column->addView(bilinearCell);

    videoModeCell = new brls::DetailCell();
    videoModeCell->setText(strings::kSettingsVideoMode);
    videoModeCell->registerClickAction([this](brls::View *) {
        brls::Application::pushActivity(new VideoModeActivity(streamType));
        return true;
    });
    updateVideoModeCellUI();
    column->addView(videoModeCell);

    auto *scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setContentView(column);

    frame = new brls::AppletFrame(scroll);
    frame->setTitle(label.c_str());
    return frame;
}
