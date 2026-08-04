package com.finlink.android

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Plain JVM unit test (no Context needed) for
 * LocaleHelper.resolveLocaleTag(). "generell: Sprach- und Bilinear-Filter-
 * Settings" test category.
 */
class LocaleHelperTest {

    @Test
    fun `explicit pref wins over system language`() {
        assertEquals("de", LocaleHelper.resolveLocaleTag("de", "en"))
        assertEquals("fr", LocaleHelper.resolveLocaleTag("fr", "es"))
    }

    @Test
    fun `system pref (empty string sentinel doesn't apply here -- LANGUAGE_SYSTEM) resolves to a supported system language`() {
        assertEquals("de", LocaleHelper.resolveLocaleTag(Prefs.LANGUAGE_SYSTEM, "de"))
    }

    @Test
    fun `system pref falls back to en for an unsupported system language`() {
        assertEquals("en", LocaleHelper.resolveLocaleTag(Prefs.LANGUAGE_SYSTEM, "ja"))
    }

    @Test
    fun `a stale unsupported pref value falls back through to system, not straight to en`() {
        assertEquals("fr", LocaleHelper.resolveLocaleTag("xx", "fr"))
    }

    @Test
    fun `both pref and system unsupported falls back to en`() {
        assertEquals("en", LocaleHelper.resolveLocaleTag(Prefs.LANGUAGE_SYSTEM, ""))
    }
}
