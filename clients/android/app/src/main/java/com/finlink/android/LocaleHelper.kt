package com.finlink.android

import android.content.Context
import android.content.res.Configuration
import android.content.res.Resources
import java.util.Locale

/**
 * Resolves Prefs.language ("system"/"de"/"en") to an actual Locale and
 * wraps a Context's Configuration with it, since this app has no
 * androidx.appcompat dependency (see build.gradle.kts's own comment on
 * staying lean) and therefore no AppCompatDelegate.setApplicationLocales()
 * to lean on. "system" resolves to German only if the device's own locale
 * is German -- anything else (including a locale Android itself couldn't
 * determine) falls back to English, same policy as every other client.
 */
object LocaleHelper {

    fun resolvedLocale(context: Context): Locale =
        when (Prefs(context).language) {
            "de" -> Locale.GERMAN
            "en" -> Locale.ENGLISH
            else -> if (systemLocale().language == "de") Locale.GERMAN else Locale.ENGLISH
        }

    fun wrap(context: Context): Context {
        val locale = resolvedLocale(context)
        Locale.setDefault(locale)
        val config = Configuration(context.resources.configuration)
        config.setLocale(locale)
        return context.createConfigurationContext(config)
    }

    private fun systemLocale(): Locale = Resources.getSystem().configuration.locales[0]
}
