// Host-buildable (plain g++/clang++, no devkitA64) unit tests for the
// "generell: Sprach- und Bilinear-Filter-Settings" test category, Switch
// client: settings_pref.hpp/.cpp -- deliberately separate from prefs.cpp
// itself, which includes <switch.h> at file scope and so can't be linked
// into a host test at all (see settings_pref.hpp's own comment).

#include "settings_pref.hpp"

#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                                    \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            std::exit(1);                                                               \
        }                                                                                \
    } while (0)

// ---------------- defaultBilinearFor ----------------

static void TestBilinearDefaultsPerStreamType() {
    CHECK(defaultBilinearFor("GC_GBA_LINK") == false);
    CHECK(defaultBilinearFor("WIIU_GAMEPAD") == true);
    CHECK(defaultBilinearFor("N3DS_BOTTOM_SCREEN") == true);
    CHECK(defaultBilinearFor("NDS_BOTTOM_SCREEN") == true);
    CHECK(defaultBilinearFor("SOME_FUTURE_STREAM_TYPE") == false);
}

// ---------------- languagePrefFromCode / languagePrefCode ----------------

static void TestLanguagePrefCodeRoundTrip() {
    struct { const char *code; Prefs::LanguagePref pref; } cases[] = {
        {"de", Prefs::LanguagePref::DE}, {"en", Prefs::LanguagePref::EN},
        {"fr", Prefs::LanguagePref::FR}, {"it", Prefs::LanguagePref::IT},
        {"es", Prefs::LanguagePref::ES},
    };
    for (const auto &c : cases) {
        CHECK(languagePrefFromCode(c.code) == c.pref);
        CHECK(std::string(languagePrefCode(c.pref)) == c.code);
    }
}

static void TestLanguagePrefFromCodeUnknownFallsBackToSystem() {
    CHECK(languagePrefFromCode("ja") == Prefs::LanguagePref::SYSTEM);
    CHECK(languagePrefFromCode("") == Prefs::LanguagePref::SYSTEM);
}

static void TestLanguagePrefCodeForSystemIsLiterallySystem() {
    CHECK(std::string(languagePrefCode(Prefs::LanguagePref::SYSTEM)) == "system");
}

// ---------------- resolveLanguagePref ----------------

static void TestExplicitPrefWinsOverSystemLanguage() {
    CHECK(resolveLanguagePref(Prefs::LanguagePref::DE, strings::Lang::EN) == strings::Lang::DE);
    CHECK(resolveLanguagePref(Prefs::LanguagePref::FR, strings::Lang::ES) == strings::Lang::FR);
}

static void TestSystemPrefUsesSystemLanguageWhenKnown() {
    CHECK(resolveLanguagePref(Prefs::LanguagePref::SYSTEM, strings::Lang::DE) == strings::Lang::DE);
    CHECK(resolveLanguagePref(Prefs::LanguagePref::SYSTEM, strings::Lang::ES) == strings::Lang::ES);
}

static void TestSystemPrefFallsBackToEnglishWhenSystemLanguageUnknown() {
    // nullopt = setGetSystemLanguage()/setMakeLanguage() failed, or
    // succeeded but reported a language this app has no translation for
    // (e.g. Japanese) -- see prefs.cpp's resolveLanguage(), the only real
    // caller and the only place that ever passes nullopt.
    CHECK(resolveLanguagePref(Prefs::LanguagePref::SYSTEM, std::nullopt) == strings::Lang::EN);
}

int main() {
    TestBilinearDefaultsPerStreamType();
    TestLanguagePrefCodeRoundTrip();
    TestLanguagePrefFromCodeUnknownFallsBackToSystem();
    TestLanguagePrefCodeForSystemIsLiterallySystem();
    TestExplicitPrefWinsOverSystemLanguage();
    TestSystemPrefUsesSystemLanguageWhenKnown();
    TestSystemPrefFallsBackToEnglishWhenSystemLanguageUnknown();
    std::printf("language_and_bilinear (switch): all tests passed\n");
    return 0;
}
