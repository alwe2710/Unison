#pragma once

#include <optional>

#include "prefs.hpp"
#include "strings_generated.hpp"

// The Prefs::LanguagePref::SYSTEM resolution decision -- pulled out of
// main.cpp's resolveLanguage() into its own free function, free of
// <3ds.h>/cfgu (unlike the rest of resolveLanguage(), which calls
// CFGU_GetSystemLanguage()), so it has one place to unit-test on a plain
// host compiler instead of only being exercisable on hardware/citra -- see
// tests/test_language_pref.cpp.
//
// systemLanguage: the console's own system language, already translated
// from libctru's raw CFG_LANGUAGE_* code by the caller (main.cpp) into
// strings::Lang -- nullopt when CFGU_GetSystemLanguage() failed, or when it
// succeeded but reported a language this app has no translation for (e.g.
// Japanese). Only actually consulted when pref is SYSTEM; an explicit
// DE/EN/FR/IT/ES always wins outright. SYSTEM + nullopt falls back to EN --
// same "English if undetermined/unsupported" policy as every other client.
strings::Lang resolveLanguagePref(Prefs::LanguagePref pref, std::optional<strings::Lang> systemLanguage);
