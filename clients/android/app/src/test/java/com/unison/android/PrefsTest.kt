package com.unison.android

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Plain JVM unit test (no Context/SharedPreferences needed) for
 * Prefs.defaultBilinearFor() -- companion-object function, callable
 * without constructing a Prefs instance. "generell: Sprach- und
 * Bilinear-Filter-Settings" test category.
 */
class PrefsTest {

    @Test
    fun `GC_GBA_LINK defaults to nearest`() {
        assertEquals(false, Prefs.defaultBilinearFor("GC_GBA_LINK"))
    }

    @Test
    fun `WIIU_GAMEPAD N3DS_BOTTOM_SCREEN NDS_BOTTOM_SCREEN default to bilinear`() {
        assertEquals(true, Prefs.defaultBilinearFor("WIIU_GAMEPAD"))
        assertEquals(true, Prefs.defaultBilinearFor("N3DS_BOTTOM_SCREEN"))
        assertEquals(true, Prefs.defaultBilinearFor("NDS_BOTTOM_SCREEN"))
    }

    @Test
    fun `unrecognized stream_type defaults to nearest, not bilinear`() {
        assertEquals(false, Prefs.defaultBilinearFor("SOME_FUTURE_STREAM_TYPE"))
    }

    @Test
    fun `empty stream_type (manual host-colon-port entry, real type unknown yet) defaults to nearest`() {
        assertEquals(false, Prefs.defaultBilinearFor(""))
    }
}
