#include "console_settings_activity.hpp"

#include <cstring>
#include <iterator>

#include "console_detail_activity.hpp"
#include "strings_generated.hpp"

namespace {
// Every stream_type docs/protocol.md currently defines ("Stream-Typen"),
// mirrored in menu_activity.cpp's own kStreamTypeGcGbaLink -- Prefs::
// bilinearFor()/setBilinearFor()/videoModeFor()/setVideoModeFor() key off
// the same raw strings, this is just the fixed set + streamType only (not
// the label itself): the strings::kConsoleXxx globals it used to embed
// directly are runtime-mutable now (repointed by strings::setLanguage()),
// so the label is resolved fresh via labelForStreamType() below instead of
// being baked in once at compile time (which is also why this can stay
// constexpr at all).
struct StreamTypeEntry {
    const char *streamType;
};
// Alphabetical by displayed label (3DS, GBA/GC, NDS, Wii U), per explicit
// request -- a fixed order rather than a dynamic sort, since all four
// console names are untranslated technical/brand terms, identical across
// every language (see i18n/strings.json), unlike LanguageActivity's own
// endonym-based sort elsewhere.
constexpr StreamTypeEntry kKnownStreamTypes[] = {
    { "N3DS_BOTTOM_SCREEN" },
    { "GC_GBA_LINK" },
    { "NDS_BOTTOM_SCREEN" },
    { "WIIU_GAMEPAD" },
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
} // namespace

// Plain console name only, no video-mode subtitle here (an earlier revision
// showed one, reverted per explicit request) -- the video mode itself is
// only ever shown/changed one screen further in, on ConsoleDetailActivity.
void ConsoleSettingsActivity::updateConsoleCellUI(int index) {
    consoleCells[index]->setText(labelForStreamType(kKnownStreamTypes[index].streamType));
}

void ConsoleSettingsActivity::onResume() {
    brls::Activity::onResume();
    frame->setTitle(strings::kSettingsConsoleSpecific);
    for (int i = 0; i < static_cast<int>(std::size(kKnownStreamTypes)); i++) {
        updateConsoleCellUI(i);
    }
}

brls::View *ConsoleSettingsActivity::createContentView() {
    auto *column = new brls::Box();
    column->setAxis(brls::Axis::COLUMN);
    column->setAlignItems(brls::AlignItems::STRETCH);
    column->setPadding(24, 32, 24, 32);

    for (int i = 0; i < static_cast<int>(std::size(kKnownStreamTypes)); i++) {
        auto *cell = new brls::DetailCell();
        const char *streamType = kKnownStreamTypes[i].streamType;
        cell->registerClickAction([streamType, label = labelForStreamType(streamType)](brls::View *) {
            brls::Application::pushActivity(new ConsoleDetailActivity(streamType, label));
            return true;
        });
        consoleCells[i] = cell;
        updateConsoleCellUI(i);
        column->addView(cell);
    }

    auto *scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setContentView(column);

    frame = new brls::AppletFrame(scroll);
    frame->setTitle(strings::kSettingsConsoleSpecific);
    return frame;
}
