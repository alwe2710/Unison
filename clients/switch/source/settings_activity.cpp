#include "settings_activity.hpp"

#include "console_settings_activity.hpp"
#include "language_activity.hpp"
#include "strings_generated.hpp"

namespace {
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
} // namespace

void SettingsActivity::updateLanguageCellUI() {
    languageCell->setDetailText(labelForLanguagePref(prefs.language));
}

void SettingsActivity::onResume() {
    brls::Activity::onResume();
    prefs = Prefs();
    updateLanguageCellUI();
    frame->setTitle(strings::kSettings);
    languageCell->setText(strings::kSettingsLanguage);
    consoleSettingsCell->setText(strings::kSettingsConsoleSpecific);
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

    consoleSettingsCell = new brls::DetailCell();
    consoleSettingsCell->setText(strings::kSettingsConsoleSpecific);
    consoleSettingsCell->registerClickAction([](brls::View *) {
        brls::Application::pushActivity(new ConsoleSettingsActivity());
        return true;
    });
    column->addView(consoleSettingsCell);

    auto *scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setContentView(column);

    frame = new brls::AppletFrame(scroll);
    frame->setTitle(strings::kSettings);
    return frame;
}
