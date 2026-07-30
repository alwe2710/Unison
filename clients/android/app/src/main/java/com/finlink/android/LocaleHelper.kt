package com.finlink.android

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
        SUPPORTED[Prefs(context).language] ?: SUPPORTED[systemLocale().language] ?: Locale.ENGLISH

    fun wrap(context: Context): Context {
        val locale = resolvedLocale(context)
        Locale.setDefault(locale)
        val config = Configuration(context.resources.configuration)
        config.setLocale(locale)
        return context.createConfigurationContext(config)
    }

    private fun systemLocale(): Locale = Resources.getSystem().configuration.locales[0]
}
