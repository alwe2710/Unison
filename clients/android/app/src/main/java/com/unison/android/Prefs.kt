package com.unison.android

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
        context.getSharedPreferences("unison_settings", Context.MODE_PRIVATE)

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
    // unison_extended_input control set (ExtButtons.kt) -- a distinct
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

    /** androidKeyCode -> unison_button_bit, for the Standard-Tasten
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

    /** General (not per-console) override for GbaStreamClient.connect() /
     * jni_bridge.c's ensure_video_codec() -- true (default) uses whatever
     * hardware/software decoder Android itself picks for the codec type;
     * false forces the named software decoder
     * (c2.android.avc.decoder/c2.android.hevc.decoder) as an intentional,
     * user-opted-in fallback, for the rare device where the hardware
     * decoder produces a distorted/skewed image (see jni_bridge.c's own
     * comment, "schief und interlaced"). */
    var hardwareDecodeEnabled: Boolean
        get() = prefs.getBoolean(PREF_HARDWARE_DECODE, true)
        set(value) = prefs.edit().putBoolean(PREF_HARDWARE_DECODE, value).apply()

    /** "system" (default, follow the device locale -- see LocaleHelper), or
     * one of LANGUAGES' language codes. A manual override from the
     * Settings language picker. */
    var language: String
        get() = prefs.getString(PREF_LANGUAGE, LANGUAGE_SYSTEM) ?: LANGUAGE_SYSTEM
        set(value) = prefs.edit().putString(PREF_LANGUAGE, value).apply()

    /** One of VIDEO_MODES' values, sent verbatim as hello_ack.video_mode
     * during the handshake (see docs/protocol.md) -- a manual override from
     * the per-console settings screen (ConsoleDetailActivity). Per
     * stream_type, same reasoning/shape as bilinearFor() below (and same
     * "" == manual host:port entry, real stream_type unknown until hello,
     * falls back to VIDEO_MODE_DEFAULT) -- used to be one single global
     * value shared by every console, which meant picking e.g. H.264 for
     * Cemu also silently requested H.264 the next time you connected to
     * Dolphin (which never supports it, always falls back, but still).
     * Servers that don't implement the negotiation at all just ignore the
     * field either way. */
    fun videoModeFor(streamType: String): String {
        val mode = prefs.getString(prefKeyForVideoMode(streamType), VIDEO_MODE_DEFAULT) ?: VIDEO_MODE_DEFAULT
        // WIIU_GAMEPAD (Cemu)/N3DS_BOTTOM_SCREEN (azahar)/NDS_BOTTOM_SCREEN
        // (melonDS) have all removed both raw video modes entirely now --
        // TILES was never actually implemented for the latter two (always a
        // full frame either way) and the plain "legacy" full-raw-frame path
        // was removed outright alongside it, matching WIIU_GAMEPAD's own
        // earlier removal (see each server's own SendVideoFrame() comment;
        // raw/tiling stays only for GC_GBA_LINK, whose native low-res
        // pixel-art content actually benefits from it) -- honest request,
        // and it also normalizes a stale "legacy"/"tiles" pref saved before
        // this existed, since videoModesFor() keeps either from ever being
        // picked again going forward.
        return if (isRawFallbackRemovedFor(streamType) && (mode == "legacy" || mode == VIDEO_MODE_DEFAULT)) "h264"
        else mode
    }

    fun setVideoModeFor(streamType: String, value: String) {
        prefs.edit().putString(prefKeyForVideoMode(streamType), value).apply()
    }

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

    /** N3DS_BOTTOM_SCREEN-only, unlike bilinearFor()/videoModeFor() above --
     * a flat flag rather than per-stream_type, since no other console has a
     * second stick to ever need this for. PlayerActivity's own
     * ExtActionButtons only ever showed the right VirtualStick gated on
     * hasSticksMode (n3ds_touch_and_buttons), which WIIU_GAMEPAD and
     * N3DS_BOTTOM_SCREEN both negotiate -- a real bug, since the actual 3DS
     * has only one circle pad. Default false to match that real hardware;
     * WIIU_GAMEPAD keeps its own always-on second stick regardless of this
     * setting (see PlayerActivity's own hasRightStick computation). */
    var n3dsSecondStickEnabled: Boolean
        get() = prefs.getBoolean(PREF_N3DS_SECOND_STICK, false)
        set(value) = prefs.edit().putBoolean(PREF_N3DS_SECOND_STICK, value).apply()

    private fun prefKeyFor(button: GbaButton) = "keybind_${button.prefKey}"
    private fun prefKeyFor(button: ExtButton) = "extkeybind_${button.prefKey}"
    private fun prefKeyForBilinear(streamType: String) = "bilinear_video_filter.$streamType"
    private fun prefKeyForVideoMode(streamType: String) = "video_mode.$streamType"

    companion object {
        /** See bilinearFor()'s own comment. Anything not in this set
         * (including "" -- manual host:port entry, whose real stream_type
         * isn't known until the handshake's hello) defaults to nearest,
         * same as GC_GBA_LINK. internal (not private) so PrefsTest can
         * exercise it directly without needing a real Context/
         * SharedPreferences -- see that file. */
        internal fun defaultBilinearFor(streamType: String): Boolean =
            streamType == "WIIU_GAMEPAD" || streamType == "N3DS_BOTTOM_SCREEN" || streamType == "NDS_BOTTOM_SCREEN"

        /** Whether PlayerActivity's second VirtualStick should show at all
         * (see its own hasRightStick property) -- WIIU_GAMEPAD's second
         * stick is real hardware, always on regardless of
         * secondStickEnabled; N3DS_BOTTOM_SCREEN's is opt-in only
         * (n3dsSecondStickEnabled's own comment on why); every other
         * stream_type has no stick at all. internal, same PrefsTest
         * reasoning as defaultBilinearFor() above. */
        internal fun hasRightStick(streamType: String, secondStickEnabled: Boolean): Boolean =
            streamType == "WIIU_GAMEPAD" || (streamType == "N3DS_BOTTOM_SCREEN" && secondStickEnabled)

        private const val PREF_ON_SCREEN_CONTROLS = "on_screen_controls"
        private const val PREF_HARDWARE_DECODE = "hardware_decode_enabled"
        private const val PREF_LANGUAGE = "language"
        private const val PREF_N3DS_SECOND_STICK = "n3ds_second_stick_enabled"
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

        const val VIDEO_MODE_DEFAULT = "tiles"

        /** Single source of truth for VideoModeActivity's list and
         * SettingsActivity's subtitle lookup, same pattern as LANGUAGES
         * above -- keep in sync with docs/protocol.md's hello_ack.video_mode.
         * Declared in a fixed, deliberate order (VideoModeActivity doesn't
         * sort this list the way LanguageActivity sorts LANGUAGES by label,
         * since these aren't endonyms), per explicit request: the two
         * raw-deflate modes first (legacy/"Raw (Deflate)" before
         * tiles/"Raw+Tiling (Deflate)"), then h264/h265. */
        data class VideoModeOption(val value: String, val labelRes: Int)
        val VIDEO_MODES = listOf(
            VideoModeOption("legacy", R.string.video_mode_legacy),
            VideoModeOption(VIDEO_MODE_DEFAULT, R.string.video_mode_tiles),
            VideoModeOption("h264", R.string.video_mode_h264),
            VideoModeOption("h265", R.string.video_mode_h265)
        )

        /** WIIU_GAMEPAD/N3DS_BOTTOM_SCREEN/NDS_BOTTOM_SCREEN -- see
         * videoModeFor()'s own comment: all three servers have dropped both
         * raw video modes (legacy/tiles) entirely, always using a real
         * h264/h265 encoder instead. GC_GBA_LINK (dolphin-gba-stream) is the
         * one stream type that still genuinely implements raw/tiling (its
         * native low-res pixel-art content actually benefits from it), so
         * it's deliberately left out here. internal, same PrefsTest
         * reasoning as hasRightStick() above. */
        internal fun isRawFallbackRemovedFor(streamType: String): Boolean =
            streamType == "WIIU_GAMEPAD" || streamType == "N3DS_BOTTOM_SCREEN" ||
                streamType == "NDS_BOTTOM_SCREEN"

        /** VideoModeActivity's actual list for a given console -- drops both
         * raw options (legacy/tiles) for any console whose own encoder no
         * longer has a fallback path for either at all; every other console
         * still gets the full VIDEO_MODES list unfiltered. internal, same
         * PrefsTest reasoning as hasRightStick() above. */
        internal fun videoModesFor(streamType: String): List<VideoModeOption> =
            if (isRawFallbackRemovedFor(streamType))
                VIDEO_MODES.filter { it.value != "legacy" && it.value != VIDEO_MODE_DEFAULT }
            else VIDEO_MODES
    }
}
