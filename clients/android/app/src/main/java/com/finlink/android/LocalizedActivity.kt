package com.finlink.android

import android.content.Context
import androidx.activity.ComponentActivity
import java.util.Locale

/**
 * Base class for every finlink Activity, wrapping attachBaseContext with
 * LocaleHelper so resource lookups (stringResource(), etc.) resolve
 * against the resolved language (system/de/en, see LocaleHelper) instead
 * of always the device's raw locale. onResume() recreates the Activity if
 * the resolved language changed since it was created (e.g. the user just
 * came back from SettingsActivity's language picker) -- an already-created
 * Activity's Configuration doesn't update itself just because a *different*
 * Activity's attachBaseContext ran with a new locale.
 */
abstract class LocalizedActivity : ComponentActivity() {

    private var createdWithLocale: Locale? = null

    override fun attachBaseContext(newBase: Context) {
        val locale = LocaleHelper.resolvedLocale(newBase)
        createdWithLocale = locale
        super.attachBaseContext(LocaleHelper.wrap(newBase))
    }

    override fun onResume() {
        super.onResume()
        if (LocaleHelper.resolvedLocale(this) != createdWithLocale) {
            recreate()
        }
    }
}
