package com.unison.android

import android.content.Context
import android.content.res.Configuration
import android.content.res.Resources
import java.util.Locale

/**
 * Resolves Prefs.language ("system"/"de"/"en"/"fr"/"it"/"es") to an actual
 * Locale and wraps a Context's Configuration with it, since this app has
 * no androidx.appcompat dependency (see build.gradle.kts's own comment on
 * staying lean) and therefore no AppCompatDelegate.setApplicationLocales()
 * to lean on. "system" resolves to whichever of those five the device's
 * own locale matches -- anything else (including a locale Android itself
 * couldn't determine) falls back to English, same policy as every other
 * client.
 */
object LocaleHelper {

    // Keep in sync with i18n/strings.json's language set and Prefs.LANGUAGES.
    private val SUPPORTED = mapOf(
        "de" to Locale.GERMAN,
        "en" to Locale.ENGLISH,
        "fr" to Locale.FRENCH,
        "it" to Locale.ITALIAN,
        "es" to Locale("es")
    )

    fun resolvedLocale(context: Context): Locale =
        SUPPORTED[resolveLocaleTag(Prefs(context).language, systemLocale().language)] ?: Locale.ENGLISH

    /** The actual pref/system-language resolution decision, pulled out into
     * its own function taking plain strings (not a Context) so it has one
     * place to unit-test on the plain JVM instead of only being
     * exercisable with a real Context -- see LocaleHelperTest.
     * "system" (default, English if undetermined/unsupported) applies
     * equally to an explicit pref that isn't actually one of SUPPORTED's
     * keys (e.g. a stale value from an older app version's Prefs). */
    internal fun resolveLocaleTag(prefLanguage: String, systemLanguageTag: String): String =
        if (SUPPORTED.containsKey(prefLanguage)) prefLanguage
        else if (SUPPORTED.containsKey(systemLanguageTag)) systemLanguageTag
        else "en"

    fun wrap(context: Context): Context {
        val locale = resolvedLocale(context)
        Locale.setDefault(locale)
        val config = Configuration(context.resources.configuration)
        config.setLocale(locale)
        return context.createConfigurationContext(config)
    }

    private fun systemLocale(): Locale = Resources.getSystem().configuration.locales[0]
}
