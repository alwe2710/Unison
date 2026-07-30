#include "language_activity.hpp"

#include <algorithm>
#include <cstring>

#include "prefs.hpp"
#include "strings_generated.hpp"

namespace {
struct LanguageOption {
    Prefs::LanguagePref pref;
    const char *label;
};
} // namespace

brls::View *LanguageActivity::createContentView() {
    auto *column = new brls::Box();
    column->setAxis(brls::Axis::COLUMN);
    column->setAlignItems(brls::AlignItems::STRETCH);
    column->setPadding(24, 32, 24, 32);

    LanguageOption options[] = {
        { Prefs::LanguagePref::SYSTEM, strings::kLanguageSystem },
        { Prefs::LanguagePref::DE, strings::kLanguageGerman },
        { Prefs::LanguagePref::EN, strings::kLanguageEnglish },
    };
    // Sorted by the displayed label, not a fixed order -- "System" is
    // localized like any other UI string, but "Deutsch"/"English" are
    // fixed endonyms (see strings.json), so this only actually reorders
    // relative to "System"/"Système"/... as more languages are added later.
    std::sort(std::begin(options), std::end(options),
        [](const LanguageOption &a, const LanguageOption &b) { return strcmp(a.label, b.label) < 0; });
    for (const auto &option : options) {
        auto *cell = new brls::DetailCell();
        cell->setText(option.label);
        cell->registerClickAction([pref = option.pref](brls::View *) {
            Prefs prefs;
            prefs.language = pref;
            prefs.save();
            applyLanguage(prefs);
            // SettingsActivity's own onResume() (called by popActivity() on
            // the activity revealed underneath) reloads Prefs and refreshes
            // its text -- see its own comment.
            brls::Application::popActivity();
            return true;
        });
        column->addView(cell);
    }

    auto *scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setContentView(column);

    auto *frame = new brls::AppletFrame(scroll);
    frame->setTitle(strings::kSettingsLanguage);
    return frame;
}
