/* Host-buildable (plain gcc/clang, no devkitARM) unit test for
 * finlink_nds_resolve_language() (language_pref.h/.c) -- the "generell:
 * Sprach- und Bilinear-Filter-Settings" test category, NDS client. (No
 * bilinear test here: g_prefBilinear is a single, un-derived toggle for
 * this client -- see its own comment in main.c -- nothing computes it, so
 * there's no logic to regression-test beyond its default value.) */

#include "language_pref.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            exit(1);                                                            \
        }                                                                         \
    } while (0)

static void test_explicit_pref_wins_over_personal_data(void) {
    /* An explicit StrLang pref (>= 0) always wins, regardless of what
     * PersonalData->language would otherwise resolve to. */
    CHECK(finlink_nds_resolve_language(STR_LANG_DE, /* personalDataLanguage (EN) */ 1) == STR_LANG_DE);
    CHECK(finlink_nds_resolve_language(STR_LANG_FR, /* personalDataLanguage (ES) */ 5) == STR_LANG_FR);
}

static void test_system_pref_maps_personal_data_language(void) {
    CHECK(finlink_nds_resolve_language(-1, /* DE */ 3) == STR_LANG_DE);
    CHECK(finlink_nds_resolve_language(-1, /* FR */ 2) == STR_LANG_FR);
    CHECK(finlink_nds_resolve_language(-1, /* IT */ 4) == STR_LANG_IT);
    CHECK(finlink_nds_resolve_language(-1, /* ES */ 5) == STR_LANG_ES);
}

static void test_system_pref_unsupported_personal_data_language_falls_back_to_english(void) {
    CHECK(finlink_nds_resolve_language(-1, /* Japanese */ 0) == STR_LANG_EN);
    CHECK(finlink_nds_resolve_language(-1, /* English (itself maps to EN anyway) */ 1) == STR_LANG_EN);
    CHECK(finlink_nds_resolve_language(-1, /* Chinese */ 6) == STR_LANG_EN);
    CHECK(finlink_nds_resolve_language(-1, /* Unknown/Reserved */ 7) == STR_LANG_EN);
}

int main(void) {
    test_explicit_pref_wins_over_personal_data();
    test_system_pref_maps_personal_data_language();
    test_system_pref_unsupported_personal_data_language_falls_back_to_english();
    printf("language_pref (nds): all tests passed\n");
    return 0;
}
