#include "language_pref.hpp"

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
