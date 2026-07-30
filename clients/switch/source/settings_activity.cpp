#include "settings_activity.hpp"

#include <cstring>
#include <iterator>

#include "language_activity.hpp"
#include "strings_generated.hpp"

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
// just the fixed set + streamType only (not the label itself): the
// strings::kConsoleXxx globals it used to embed directly are runtime-
// mutable now (repointed by strings::setLanguage()), so the label is
// resolved fresh via labelForStreamType() below instead of being baked in
// once at compile time (which is also why this can stay constexpr at all).
struct StreamTypeEntry {
    const char *streamType;
};
constexpr StreamTypeEntry kKnownStreamTypes[] = {
    { "GC_GBA_LINK" },
    { "WIIU_GAMEPAD" },
    { "N3DS_BOTTOM_SCREEN" },
    { "NDS_BOTTOM_SCREEN" },
};

const char *labelForStreamType(const char *streamType) {
    if (strcmp(streamType, "GC_GBA_LINK") == 0) {
        return strings::kConsoleGcGbaLink;
    }
    if (strcmp(streamType, "WIIU_GAMEPAD") == 0) {
        return strings::kConsoleWiiuGamepad;
    }
    if (strcmp(streamType, "N3DS_BOTTOM_SCREEN") == 0) {
        return strings::kConsoleN3dsBottomScreen;
    }
    return strings::kConsoleNdsBottomScreen; // NDS_BOTTOM_SCREEN
}

const char *labelForLanguagePref(Prefs::LanguagePref pref) {
    switch (pref) {
    case Prefs::LanguagePref::DE:
        return strings::kLanguageGerman;
    case Prefs::LanguagePref::EN:
        return strings::kLanguageEnglish;
    default:
        return strings::kLanguageSystem;
    }
}
} // namespace

void SettingsActivity::updateFilterCellUI(int index) {
    bool on = prefs.bilinearFor(kKnownStreamTypes[index].streamType);
    filterCells[index]->setDetailText(on ? strings::kFilterOn : strings::kFilterOff);
    filterCells[index]->setDetailTextColor(on ? kColorOn : kColorOff);
}

void SettingsActivity::updateLanguageCellUI() {
    languageCell->setDetailText(labelForLanguagePref(prefs.language));
}

void SettingsActivity::onResume() {
    brls::Activity::onResume();
    prefs = Prefs();
    updateLanguageCellUI();
    header->setText(strings::kSettingsAntialiasing);
    frame->setTitle(strings::kSettings);
    languageCell->setText(strings::kSettingsLanguage);
    for (int i = 0; i < static_cast<int>(std::size(kKnownStreamTypes)); i++) {
        filterCells[i]->setText(labelForStreamType(kKnownStreamTypes[i].streamType));
        updateFilterCellUI(i);
    }
}

brls::View *SettingsActivity::createContentView() {
    auto *column = new brls::Box();
    column->setAxis(brls::Axis::COLUMN);
    column->setAlignItems(brls::AlignItems::STRETCH);
    column->setPadding(24, 32, 24, 32);

    languageCell = new brls::DetailCell();
    languageCell->setText(strings::kSettingsLanguage);
    languageCell->registerClickAction([](brls::View *) {
        brls::Application::pushActivity(new LanguageActivity());
        return true;
    });
    updateLanguageCellUI();
    column->addView(languageCell);

    header = new brls::Label();
    header->setText(strings::kSettingsAntialiasing);
    header->setFontSize(16);
    column->addView(header);

    for (int i = 0; i < static_cast<int>(std::size(kKnownStreamTypes)); i++) {
        auto *cell = new brls::DetailCell();
        cell->setText(labelForStreamType(kKnownStreamTypes[i].streamType));
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

    frame = new brls::AppletFrame(scroll);
    frame->setTitle(strings::kSettings);
    return frame;
}
