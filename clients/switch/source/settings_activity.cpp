#include "settings_activity.hpp"

#include <cstring>
#include <iterator>

#include "language_activity.hpp"
#include "strings_generated.hpp"
#include "video_mode_activity.hpp"

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
    case Prefs::LanguagePref::FR:
        return strings::kLanguageFrench;
    case Prefs::LanguagePref::IT:
        return strings::kLanguageItalian;
    case Prefs::LanguagePref::ES:
        return strings::kLanguageSpanish;
    default:
        return strings::kLanguageSystem;
    }
}

// Wire-format string (finlink/docs/protocol.md) -> label -- mirrors
// labelForLanguagePref() above, just keyed on a raw std::string instead of
// an enum since video_mode never had one (see Prefs::videoMode's own
// comment). Falls back to "tiles" for anything unrecognized, same as a
// server would.
const char *labelForVideoMode(const std::string &mode) {
    if (mode == "h264") return strings::kVideoModeH264;
    if (mode == "h265") return strings::kVideoModeH265;
    if (mode == "legacy") return strings::kVideoModeLegacy;
    return strings::kVideoModeTiles;
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

void SettingsActivity::updateVideoModeCellUI() {
    videoModeCell->setDetailText(labelForVideoMode(prefs.videoMode));
}

void SettingsActivity::onResume() {
    brls::Activity::onResume();
    prefs = Prefs();
    updateLanguageCellUI();
    updateVideoModeCellUI();
    header->setText(strings::kSettingsAntialiasing);
    frame->setTitle(strings::kSettings);
    languageCell->setText(strings::kSettingsLanguage);
    videoModeCell->setText(strings::kSettingsVideoMode);
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

    videoModeCell = new brls::DetailCell();
    videoModeCell->setText(strings::kSettingsVideoMode);
    videoModeCell->registerClickAction([](brls::View *) {
        brls::Application::pushActivity(new VideoModeActivity());
        return true;
    });
    updateVideoModeCellUI();
    column->addView(videoModeCell);

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
