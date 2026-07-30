package com.finlink.android

import android.content.Context
import android.content.SharedPreferences

/**
 * Settings, all set from SettingsActivity and read by PlayerActivity: an
 * optional physical-key (keyboard/game controller) binding per GBA button,
 * whether the on-screen touch overlay is shown, and whether upscaled video
 * uses bilinear or nearest-neighbor filtering.
 */
class Prefs(context: Context) {

    private val prefs: SharedPreferences =
        context.getSharedPreferences("finlink_settings", Context.MODE_PRIVATE)

    fun getKeyBinding(button: GbaButton): Int? {
        val value = prefs.getInt(prefKeyFor(button), NO_KEYCODE)
        return if (value == NO_KEYCODE) null else value
    }

    fun setKeyBinding(button: GbaButton, androidKeyCode: Int) {
        prefs.edit().putInt(prefKeyFor(button), androidKeyCode).apply()
    }

    fun clearKeyBinding(button: GbaButton) {
        prefs.edit().remove(prefKeyFor(button)).apply()
    }

    /** androidKeyCode -> GBA button bit, only for buttons that have a binding set. */
    fun keyBindingsByKeyCode(): Map<Int, Int> =
        GBA_BUTTONS.mapNotNull { button -> getKeyBinding(button)?.let { it to button.bit } }.toMap()

    // Same shape as the GbaButton trio above, for the separate
    // finlink_extended_input control set (ExtButtons.kt) -- a distinct
    // SharedPreferences key prefix ("extkeybind_" vs "keybind_") so an
    // ExtButton and a GbaButton that happen to share a prefKey string (e.g.
    // both have an "A") never collide, even though the two binding sets are
    // otherwise never active in the same session (gba_buttons and
    // hasButtonsMode are mutually exclusive per PlayerActivity.onConnected).
    fun getKeyBinding(button: ExtButton): Int? {
        val value = prefs.getInt(prefKeyFor(button), NO_KEYCODE)
        return if (value == NO_KEYCODE) null else value
    }

    fun setKeyBinding(button: ExtButton, androidKeyCode: Int) {
        prefs.edit().putInt(prefKeyFor(button), androidKeyCode).apply()
    }

    fun clearKeyBinding(button: ExtButton) {
        prefs.edit().remove(prefKeyFor(button)).apply()
    }

    /** androidKeyCode -> ExtButton, only for controls that have a binding
     * set. Keyed by the whole ExtButton (not just a bit, unlike
     * keyBindingsByKeyCode() above) since PlayerActivity needs to tell a
     * STICK_* entry apart from a BUTTON one to know which state to update. */
    fun extKeyBindingsByKeyCode(): Map<Int, ExtButton> =
        (EXT_BUTTONS + EXT_BUTTONS_LIMITED).mapNotNull { button -> getKeyBinding(button)?.let { it to button } }.toMap()

    /** androidKeyCode -> finlink_button_bit, for the Standard-Tasten
     * (GBA_BUTTONS) bindings that double as a hasButtonsMode button too
     * (ExtButtons.kt's GBA_PREFKEY_TO_EXT_BUTTON_BIT) -- lets PlayerActivity
     * honor one physical-key binding for both wire encodings. */
    fun sharedExtButtonBitsByKeyCode(): Map<Int, Int> =
        GBA_BUTTONS.mapNotNull { button ->
            val extBit = GBA_PREFKEY_TO_EXT_BUTTON_BIT[button.prefKey] ?: return@mapNotNull null
            getKeyBinding(button)?.let { it to extBit }
        }.toMap()

    var onScreenControlsEnabled: Boolean
        get() = prefs.getBoolean(PREF_ON_SCREEN_CONTROLS, true)
        set(value) = prefs.edit().putBoolean(PREF_ON_SCREEN_CONTROLS, value).apply()

    /** "system" (default, follow the device locale -- see LocaleHelper), or
     * one of LANGUAGES' language codes. A manual override from the
     * Settings language picker. */
    var language: String
        get() = prefs.getString(PREF_LANGUAGE, LANGUAGE_SYSTEM) ?: LANGUAGE_SYSTEM
        set(value) = prefs.edit().putString(PREF_LANGUAGE, value).apply()

    /** true = bilinear filtering (smooth upscale), false = nearest-neighbor
     * filtering (crisp/pixelated upscale). Per stream_type ("GC_GBA_LINK",
     * "WIIU_GAMEPAD", ...) rather than one global toggle -- see
     * SettingsActivity's per-console list and PlayerActivity's
     * EXTRA_STREAM_TYPE (MenuActivity knows the stream_type before
     * launching PlayerActivity either way). A type not yet explicitly set
     * by the user falls back to defaultBilinearFor(): GC_GBA_LINK's GBA
     * output is native-resolution pixel art (nearest-neighbor looks
     * right), while WIIU_GAMEPAD/N3DS_BOTTOM_SCREEN/NDS_BOTTOM_SCREEN are
     * already-upscaled/higher-effective-resolution renders that read
     * better smoothed. */
    fun bilinearFor(streamType: String): Boolean =
        prefs.getBoolean(prefKeyForBilinear(streamType), defaultBilinearFor(streamType))

    fun setBilinearFor(streamType: String, value: Boolean) {
        prefs.edit().putBoolean(prefKeyForBilinear(streamType), value).apply()
    }

    private fun prefKeyFor(button: GbaButton) = "keybind_${button.prefKey}"
    private fun prefKeyFor(button: ExtButton) = "extkeybind_${button.prefKey}"
    private fun prefKeyForBilinear(streamType: String) = "bilinear_video_filter.$streamType"

    companion object {
        /** See bilinearFor()'s own comment. Anything not in this set
         * (including "" -- manual host:port entry, whose real stream_type
         * isn't known until the handshake's hello) defaults to nearest,
         * same as GC_GBA_LINK. */
        private fun defaultBilinearFor(streamType: String): Boolean =
            streamType == "WIIU_GAMEPAD" || streamType == "N3DS_BOTTOM_SCREEN" || streamType == "NDS_BOTTOM_SCREEN"

        private const val PREF_ON_SCREEN_CONTROLS = "on_screen_controls"
        private const val PREF_LANGUAGE = "language"
        private const val NO_KEYCODE = -1

        const val LANGUAGE_SYSTEM = "system"

        /** Single source of truth for LanguageActivity's list and
         * SettingsActivity's subtitle lookup -- keep in sync with
         * i18n/strings.json's language set (and LocaleHelper.SUPPORTED). */
        data class LanguageOption(val value: String, val labelRes: Int)
        val LANGUAGES = listOf(
            LanguageOption(LANGUAGE_SYSTEM, R.string.language_system),
            LanguageOption("de", R.string.language_german),
            LanguageOption("en", R.string.language_english),
            LanguageOption("fr", R.string.language_french),
            LanguageOption("it", R.string.language_italian),
            LanguageOption("es", R.string.language_spanish)
        )
    }
}
