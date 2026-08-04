#ifndef FINLINK_NDS_LANGUAGE_PREF_H
#define FINLINK_NDS_LANGUAGE_PREF_H

#include "strings_generated.h"

/* The g_prefLanguage/PersonalData->language resolution decision -- pulled
 * out of main.c's applyLanguage() into its own free function, free of
 * <nds/system.h> (unlike the rest of applyLanguage(), which reads
 * PersonalData directly), so it has one place to unit-test on a plain
 * host compiler instead of only being exercisable on hardware/melonDS --
 * see tests/test_language_pref.c.
 *
 * prefLanguage: g_prefLanguage's own convention, -1 = System (default),
 * otherwise a StrLang value already picked from languageMenu().
 *
 * personalDataLanguage: PersonalData->language's raw libnds encoding
 * (nds/system.h), only actually consulted when prefLanguage is -1: 0=
 * Japanese, 1=English, 2=French, 3=German, 4=Italian, 5=Spanish,
 * 6=Chinese, 7=Unknown/Reserved -- anything this app has no translation
 * for (including Japanese/Chinese/Unknown) falls back to English, same
 * policy as every other client. */
StrLang finlink_nds_resolve_language(int prefLanguage, int personalDataLanguage);

#endif /* FINLINK_NDS_LANGUAGE_PREF_H */
