#pragma once

#include <optional>
#include <string>

#include "prefs.hpp"
#include "strings_generated.hpp"

// Pure logic pulled out of prefs.cpp -- unlike the 3DS/NDS clients' own
// equivalents, prefs.cpp here includes <switch.h> at file scope (for
// setInitialize()/setGetSystemLanguage()/etc.), so none of it can be
// linked into a host test directly; this is a second, deliberately
// switch.h-free translation unit instead, so these decisions still have
// one place to unit-test on a plain host compiler -- see
// tests/test_language_and_bilinear.cpp.

// GC_GBA_LINK's GBA output is native-resolution pixel art (nearest-
// neighbor looks right), while WIIU_GAMEPAD/N3DS_BOTTOM_SCREEN/
// NDS_BOTTOM_SCREEN are already-upscaled/higher-effective-resolution
// renders that read better smoothed.
bool defaultBilinearFor(const std::string &streamType);

// Keep in sync with i18n/strings.json's language set.
Prefs::LanguagePref languagePrefFromCode(const std::string &code);
const char *languagePrefCode(Prefs::LanguagePref pref);

// The Prefs::LanguagePref::SYSTEM resolution decision -- see prefs.cpp's
// resolveLanguage(), which does the actual setGetSystemLanguage()/
// setMakeLanguage() calls and translates the result into
// std::optional<strings::Lang> before calling this. nullopt = the calls
// failed, or succeeded but reported a language this app has no
// translation for -- falls back to EN, same policy as every other client.
strings::Lang resolveLanguagePref(Prefs::LanguagePref pref, std::optional<strings::Lang> systemLanguage);
