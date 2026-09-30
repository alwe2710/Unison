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

    @Test
    fun `WIIU_GAMEPAD always has a right stick, regardless of the N3DS setting`() {
        assertEquals(true, Prefs.hasRightStick("WIIU_GAMEPAD", secondStickEnabled = false))
        assertEquals(true, Prefs.hasRightStick("WIIU_GAMEPAD", secondStickEnabled = true))
    }

    @Test
    fun `N3DS_BOTTOM_SCREEN's right stick follows the opt-in setting`() {
        assertEquals(false, Prefs.hasRightStick("N3DS_BOTTOM_SCREEN", secondStickEnabled = false))
        assertEquals(true, Prefs.hasRightStick("N3DS_BOTTOM_SCREEN", secondStickEnabled = true))
    }

    @Test
    fun `NDS_BOTTOM_SCREEN and GC_GBA_LINK never get a right stick, even with the setting on`() {
        assertEquals(false, Prefs.hasRightStick("NDS_BOTTOM_SCREEN", secondStickEnabled = true))
        assertEquals(false, Prefs.hasRightStick("GC_GBA_LINK", secondStickEnabled = true))
    }

    @Test
    fun `WIIU_GAMEPAD N3DS_BOTTOM_SCREEN NDS_BOTTOM_SCREEN video mode lists drop both raw options`() {
        for (streamType in listOf("WIIU_GAMEPAD", "N3DS_BOTTOM_SCREEN", "NDS_BOTTOM_SCREEN")) {
            val values = Prefs.videoModesFor(streamType).map { it.value }
            assertEquals(listOf("h264", "h265"), values)
        }
    }

    @Test
    fun `GC_GBA_LINK and manual entry keep all four video mode options`() {
        for (streamType in listOf("GC_GBA_LINK", "")) {
            assertEquals(Prefs.VIDEO_MODES, Prefs.videoModesFor(streamType))
        }
    }
}
