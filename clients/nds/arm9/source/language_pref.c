#include "language_pref.h"

StrLang unison_nds_resolve_language(int prefLanguage, int personalDataLanguage) {
    if (prefLanguage >= 0) {
        return (StrLang)prefLanguage;
    }
    switch (personalDataLanguage) {
    case 3: return STR_LANG_DE;
    case 2: return STR_LANG_FR;
    case 4: return STR_LANG_IT;
    case 5: return STR_LANG_ES;
    default: return STR_LANG_EN;
    }
}
