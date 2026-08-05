import Foundation

/// Settings, all set from SettingsView and read by PlayerView: an optional
/// physical-key (external keyboard/game controller) binding per GBA
/// button, whether the on-screen touch overlay is shown, and whether
/// upscaled video uses bilinear or nearest-neighbor filtering. Direct port
/// of clients/android/.../Prefs.kt -- see that file's own comment for the
/// full rationale of each piece; kept structurally identical (same key
/// names, same defaults) rather than reinvented, per this client's own
/// design goal.
final class Prefs {
    private let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
    }

    // MARK: - Key bindings (GBA_BUTTONS)

    /// Stores a UIKeyboardHIDUsage raw value (external keyboard) --
    /// KeyBindingsView is the only place that interprets what the Int
    /// actually means, same separation as Android storing a raw
    /// android.view.KeyEvent keycode.
    func keyBinding(for button: GbaButton) -> Int? {
        let key = prefKey(for: button)
        guard defaults.object(forKey: key) != nil else { return nil }
        return defaults.integer(forKey: key)
    }

    func setKeyBinding(_ code: Int, for button: GbaButton) {
        defaults.set(code, forKey: prefKey(for: button))
    }

    func clearKeyBinding(for button: GbaButton) {
        defaults.removeObject(forKey: prefKey(for: button))
    }

    /// keyCode -> GBA button bit, only for buttons that have a binding set.
    func keyBindingsByKeyCode() -> [Int: Int] {
        var result: [Int: Int] = [:]
        for button in GBA_BUTTONS {
            if let code = keyBinding(for: button) {
                result[code] = button.bit
            }
        }
        return result
    }

    // MARK: - Key bindings (ExtButtons -- unison_extended_input)

    /// Separate "extkeybind_" prefix (vs "keybind_" above) so an ExtButton
    /// and a GbaButton sharing a prefKey string never collide, mirroring
    /// Prefs.kt's own comment.
    func keyBinding(for button: ExtButton) -> Int? {
        let key = prefKey(for: button)
        guard defaults.object(forKey: key) != nil else { return nil }
        return defaults.integer(forKey: key)
    }

    func setKeyBinding(_ code: Int, for button: ExtButton) {
        defaults.set(code, forKey: prefKey(for: button))
    }

    func clearKeyBinding(for button: ExtButton) {
        defaults.removeObject(forKey: prefKey(for: button))
    }

    /// keyCode -> ExtButton (not just a bit, unlike keyBindingsByKeyCode()
    /// above) since PlayerView needs to tell a stick-direction entry apart
    /// from a plain button to know which state to update.
    func extKeyBindingsByKeyCode() -> [Int: ExtButton] {
        var result: [Int: ExtButton] = [:]
        for button in EXT_BUTTONS + EXT_BUTTONS_LIMITED {
            if let code = keyBinding(for: button) {
                result[code] = button
            }
        }
        return result
    }

    /// keyCode -> unison_button_bit, for Standard-Tasten (GBA_BUTTONS)
    /// bindings that double as a hasButtonsMode button too
    /// (GBA_PREFKEY_TO_EXT_BUTTON_BIT) -- lets PlayerView honor one
    /// physical-key binding for both wire encodings.
    func sharedExtButtonBitsByKeyCode() -> [Int: Int] {
        var result: [Int: Int] = [:]
        for button in GBA_BUTTONS {
            guard let extBit = GBA_PREFKEY_TO_EXT_BUTTON_BIT[button.prefKey] else { continue }
            if let code = keyBinding(for: button) {
                result[code] = extBit
            }
        }
        return result
    }

    // MARK: - Simple settings

    var onScreenControlsEnabled: Bool {
        get { defaults.object(forKey: Keys.onScreenControls) == nil ? true : defaults.bool(forKey: Keys.onScreenControls) }
        set { defaults.set(newValue, forKey: Keys.onScreenControls) }
    }

    /// "system" (default, follow the device locale -- see LocaleHelper), or
    /// one of Prefs.languages' language codes.
    var language: String {
        get { defaults.string(forKey: Keys.language) ?? Prefs.languageSystem }
        set { defaults.set(newValue, forKey: Keys.language) }
    }

    /// One of Prefs.videoModes' values, sent verbatim as
    /// hello_ack.video_mode during the handshake (see docs/protocol.md).
    var videoMode: String {
        get { defaults.string(forKey: Keys.videoMode) ?? Prefs.videoModeDefault }
        set { defaults.set(newValue, forKey: Keys.videoMode) }
    }

    /// true = bilinear filtering (smooth upscale), false = nearest-neighbor
    /// (crisp/pixelated upscale). Per stream_type, same rationale as
    /// Prefs.kt's own comment.
    func bilinear(for streamType: String) -> Bool {
        let key = prefKeyForBilinear(streamType)
        guard defaults.object(forKey: key) != nil else { return Prefs.defaultBilinear(for: streamType) }
        return defaults.bool(forKey: key)
    }

    func setBilinear(_ value: Bool, for streamType: String) {
        defaults.set(value, forKey: prefKeyForBilinear(streamType))
    }

    // MARK: - Private helpers

    private func prefKey(for button: GbaButton) -> String { "keybind_\(button.prefKey)" }
    private func prefKey(for button: ExtButton) -> String { "extkeybind_\(button.prefKey)" }
    private func prefKeyForBilinear(_ streamType: String) -> String { "bilinear_video_filter.\(streamType)" }

    // MARK: - Static data (single source of truth, mirrors Prefs.kt's companion object)

    /// See bilinear(for:)'s own comment. Anything not in this set
    /// (including "" -- manual host:port entry, whose real stream_type
    /// isn't known until the handshake's hello) defaults to nearest, same
    /// as GC_GBA_LINK.
    static func defaultBilinear(for streamType: String) -> Bool {
        streamType == "WIIU_GAMEPAD" || streamType == "N3DS_BOTTOM_SCREEN" || streamType == "NDS_BOTTOM_SCREEN"
    }

    static let languageSystem = "system"

    struct LanguageOption {
        let value: String
        let labelKey: String
    }
    static let languages: [LanguageOption] = [
        LanguageOption(value: languageSystem, labelKey: "language_system"),
        LanguageOption(value: "de", labelKey: "language_german"),
        LanguageOption(value: "en", labelKey: "language_english"),
        LanguageOption(value: "fr", labelKey: "language_french"),
        LanguageOption(value: "it", labelKey: "language_italian"),
        LanguageOption(value: "es", labelKey: "language_spanish"),
    ]

    static let videoModeDefault = "tiles"

    struct VideoModeOption {
        let value: String
        let labelKey: String
    }
    /// Fixed, deliberate order (not sorted): recommended default first,
    /// fallback modes after -- same as Prefs.kt's VIDEO_MODES.
    static let videoModes: [VideoModeOption] = [
        VideoModeOption(value: videoModeDefault, labelKey: "video_mode_tiles"),
        VideoModeOption(value: "h264", labelKey: "video_mode_h264"),
        VideoModeOption(value: "h265", labelKey: "video_mode_h265"),
        VideoModeOption(value: "legacy", labelKey: "video_mode_legacy"),
    ]

    private enum Keys {
        static let onScreenControls = "on_screen_controls"
        static let language = "language"
        static let videoMode = "video_mode"
    }
}
