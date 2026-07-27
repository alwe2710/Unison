#include "settings_activity.hpp"

namespace {
// Same on/off color convention as the Switch's own system settings: gray
// for off, turquoise for on -- text-based ("Ein"/"Aus"), not a graphical
// switch (the custom-drawn ToggleSwitch this replaced didn't reliably
// register clicks; DetailCell is the same proven, already-working
// clickable/focusable row used everywhere else in Menu/Settings).
constexpr NVGcolor kColorOff = { { { 150.0f / 255.0f, 150.0f / 255.0f, 150.0f / 255.0f, 1.0f } } };
constexpr NVGcolor kColorOn = { { { 0.0f / 255.0f, 195.0f / 255.0f, 195.0f / 255.0f, 1.0f } } };
} // namespace

void SettingsActivity::updateFilterCellUI() {
    filterCell->setDetailText(prefs.bilinearVideoFilter ? "Ein" : "Aus");
    filterCell->setDetailTextColor(prefs.bilinearVideoFilter ? kColorOn : kColorOff);
}

brls::View *SettingsActivity::createContentView() {
    auto *column = new brls::Box();
    column->setAxis(brls::Axis::COLUMN);
    column->setAlignItems(brls::AlignItems::STRETCH);
    column->setPadding(24, 32, 24, 32);

    filterCell = new brls::DetailCell();
    filterCell->setText("Bilineare Filterung");
    filterCell->registerClickAction([this](brls::View *) {
        prefs.bilinearVideoFilter = !prefs.bilinearVideoFilter;
        prefs.save();
        updateFilterCellUI();
        return true;
    });
    updateFilterCellUI();
    column->addView(filterCell);

    auto *scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setContentView(column);

    auto *frame = new brls::AppletFrame(scroll);
    frame->setTitle("Einstellungen");
    return frame;
}
