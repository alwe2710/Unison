#include "settings_activity.hpp"

#include <iterator>

namespace {
// Same on/off color convention as the Switch's own system settings: gray
// for off, turquoise for on -- text-based ("Ein"/"Aus"), not a graphical
// switch (the custom-drawn ToggleSwitch this replaced didn't reliably
// register clicks; DetailCell is the same proven, already-working
// clickable/focusable row used everywhere else in Menu/Settings).
constexpr NVGcolor kColorOff = { { { 150.0f / 255.0f, 150.0f / 255.0f, 150.0f / 255.0f, 1.0f } } };
constexpr NVGcolor kColorOn = { { { 0.0f / 255.0f, 195.0f / 255.0f, 195.0f / 255.0f, 1.0f } } };

// Every stream_type docs/protocol.md currently defines ("Stream-Typen"),
// mirrored in menu_activity.cpp's own kStreamTypeGcGbaLink -- Prefs::
// bilinearFor()/setBilinearFor() key off the same raw strings, this is
// just the fixed set + a human-readable label for each row below.
struct StreamTypeEntry {
    const char *streamType;
    const char *label;
};
constexpr StreamTypeEntry kKnownStreamTypes[] = {
    { "GC_GBA_LINK", "GameCube (GBA-Link)" },
    { "WIIU_GAMEPAD", "Wii U GamePad" },
    { "N3DS_BOTTOM_SCREEN", "3DS" },
    { "NDS_BOTTOM_SCREEN", "DS" },
};
} // namespace

void SettingsActivity::updateFilterCellUI(int index) {
    bool on = prefs.bilinearFor(kKnownStreamTypes[index].streamType);
    filterCells[index]->setDetailText(on ? "Ein" : "Aus");
    filterCells[index]->setDetailTextColor(on ? kColorOn : kColorOff);
}

brls::View *SettingsActivity::createContentView() {
    auto *column = new brls::Box();
    column->setAxis(brls::Axis::COLUMN);
    column->setAlignItems(brls::AlignItems::STRETCH);
    column->setPadding(24, 32, 24, 32);

    auto *header = new brls::Label();
    header->setText("Bilineare Filterung");
    header->setFontSize(16);
    column->addView(header);

    for (int i = 0; i < static_cast<int>(std::size(kKnownStreamTypes)); i++) {
        auto *cell = new brls::DetailCell();
        cell->setText(kKnownStreamTypes[i].label);
        cell->registerClickAction([this, i](brls::View *) {
            prefs.setBilinearFor(kKnownStreamTypes[i].streamType, !prefs.bilinearFor(kKnownStreamTypes[i].streamType));
            prefs.save();
            updateFilterCellUI(i);
            return true;
        });
        filterCells[i] = cell;
        updateFilterCellUI(i);
        column->addView(cell);
    }

    auto *scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setContentView(column);

    auto *frame = new brls::AppletFrame(scroll);
    frame->setTitle("Einstellungen");
    return frame;
}
