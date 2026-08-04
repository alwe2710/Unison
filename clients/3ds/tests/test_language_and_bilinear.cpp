// Host-buildable (plain g++/clang++, no devkitARM) unit tests for the
// "generell: Sprach- und Bilinear-Filter-Settings" test category, 3DS
// client: resolveLanguagePref() (language_pref.hpp/.cpp) and
// Prefs::bilinearFor()/setBilinearFor() (prefs.hpp/.cpp, already
// deliberately free of 3ds.h).

#include "language_pref.hpp"
#include "prefs.hpp"

#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                                    \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            std::exit(1);                                                               \
        }                                                                                \
    } while (0)

// ---------------- resolveLanguagePref ----------------

static void TestExplicitPrefWinsOverSystemLanguage() {
    // An explicit DE/EN/FR/IT/ES always wins, regardless of what the
    // system language would otherwise resolve to.
    CHECK(resolveLanguagePref(Prefs::LanguagePref::DE, strings::Lang::EN) == strings::Lang::DE);
    CHECK(resolveLanguagePref(Prefs::LanguagePref::FR, strings::Lang::ES) == strings::Lang::FR);
}

static void TestSystemPrefUsesSystemLanguageWhenKnown() {
    CHECK(resolveLanguagePref(Prefs::LanguagePref::SYSTEM, strings::Lang::DE) == strings::Lang::DE);
    CHECK(resolveLanguagePref(Prefs::LanguagePref::SYSTEM, strings::Lang::ES) == strings::Lang::ES);
}

static void TestSystemPrefFallsBackToEnglishWhenSystemLanguageUnknown() {
    // nullopt = CFGU_GetSystemLanguage() failed, or reported a language
    // this app has no translation for (e.g. Japanese) -- see main.cpp's
    // resolveLanguage(), which is the only real caller and the only place
    // that ever passes nullopt.
    CHECK(resolveLanguagePref(Prefs::LanguagePref::SYSTEM, std::nullopt) == strings::Lang::EN);
}

// ---------------- Prefs::bilinearFor / setBilinearFor ----------------

static void TestBilinearDefaultsPerStreamType() {
    Prefs prefs;
    CHECK(prefs.bilinearFor("GC_GBA_LINK") == false);
    CHECK(prefs.bilinearFor("WIIU_GAMEPAD") == true);
    CHECK(prefs.bilinearFor("N3DS_BOTTOM_SCREEN") == true);
    CHECK(prefs.bilinearFor("NDS_BOTTOM_SCREEN") == true);
    // Unrecognized stream_type defaults to nearest (false), not bilinear.
    CHECK(prefs.bilinearFor("SOME_FUTURE_STREAM_TYPE") == false);
}

static void TestSetBilinearForOverridesDefault() {
    Prefs prefs;
    prefs.setBilinearFor("WIIU_GAMEPAD", false);
    CHECK(prefs.bilinearFor("WIIU_GAMEPAD") == false);
    prefs.setBilinearFor("GC_GBA_LINK", true);
    CHECK(prefs.bilinearFor("GC_GBA_LINK") == true);
    // Setting one stream_type's pref doesn't affect another's default.
    CHECK(prefs.bilinearFor("N3DS_BOTTOM_SCREEN") == true);
}

int main() {
    TestExplicitPrefWinsOverSystemLanguage();
    TestSystemPrefUsesSystemLanguageWhenKnown();
    TestSystemPrefFallsBackToEnglishWhenSystemLanguageUnknown();
    TestBilinearDefaultsPerStreamType();
    TestSetBilinearForOverridesDefault();
    std::printf("language_and_bilinear (3ds): all tests passed\n");
    return 0;
}
