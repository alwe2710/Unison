#include "settings_pref.hpp"

bool defaultBilinearFor(const std::string &streamType) {
    return streamType == "WIIU_GAMEPAD" || streamType == "N3DS_BOTTOM_SCREEN" || streamType == "NDS_BOTTOM_SCREEN";
}

Prefs::LanguagePref languagePrefFromCode(const std::string &code) {
    if (code == "de") return Prefs::LanguagePref::DE;
    if (code == "en") return Prefs::LanguagePref::EN;
    if (code == "fr") return Prefs::LanguagePref::FR;
    if (code == "it") return Prefs::LanguagePref::IT;
    if (code == "es") return Prefs::LanguagePref::ES;
    return Prefs::LanguagePref::SYSTEM;
}

const char *languagePrefCode(Prefs::LanguagePref pref) {
    switch (pref) {
    case Prefs::LanguagePref::DE: return "de";
    case Prefs::LanguagePref::EN: return "en";
    case Prefs::LanguagePref::FR: return "fr";
    case Prefs::LanguagePref::IT: return "it";
    case Prefs::LanguagePref::ES: return "es";
    default: return "system";
    }
}

strings::Lang resolveLanguagePref(Prefs::LanguagePref pref, std::optional<strings::Lang> systemLanguage) {
    switch (pref) {
    case Prefs::LanguagePref::DE: return strings::Lang::DE;
    case Prefs::LanguagePref::EN: return strings::Lang::EN;
    case Prefs::LanguagePref::FR: return strings::Lang::FR;
    case Prefs::LanguagePref::IT: return strings::Lang::IT;
    case Prefs::LanguagePref::ES: return strings::Lang::ES;
    default: break;
    }
    return systemLanguage.value_or(strings::Lang::EN);
}
