import Foundation

/// UIKeyboardHIDUsage has no built-in friendly-name API (unlike Android's
/// KeyEvent.keyCodeToString(), which KeyBindingsActivity.kt calls
/// directly) -- raw values are the standard USB-IF HID Usage Tables
/// (Keyboard/Keypad page 0x07) codes, stable/documented independently of
/// UIKit, so hardcoding the common ones here is safe. Deliberately NOT
/// exhaustive (real, documented gap, not silently faked): anything not in
/// this table falls back to "Key <code>" rather than guessing:
/// numpad/media/international keys and the full punctuation set aren't
/// covered, since binding a GBA button to one of those is a rare enough
/// case that a numeric fallback is an acceptable, honest answer.
enum KeyBindingName {
    private static let names: [Int: String] = {
        var m: [Int: String] = [:]
        // A-Z: 0x04-0x1D sequential.
        for (offset, letter) in "ABCDEFGHIJKLMNOPQRSTUVWXYZ".enumerated() {
            m[0x04 + offset] = String(letter)
        }
        // 1-9, then 0: 0x1E-0x27 sequential.
        for (offset, digit) in "123456789".enumerated() {
            m[0x1E + offset] = String(digit)
        }
        m[0x27] = "0"
        m[0x28] = "Return"
        m[0x29] = "Escape"
        m[0x2A] = "Backspace"
        m[0x2B] = "Tab"
        m[0x2C] = "Space"
        m[0x4F] = "Right"
        m[0x50] = "Left"
        m[0x51] = "Down"
        m[0x52] = "Up"
        m[0xE0] = "Left Control"
        m[0xE1] = "Left Shift"
        m[0xE2] = "Left Alt"
        m[0xE3] = "Left Cmd"
        m[0xE4] = "Right Control"
        m[0xE5] = "Right Shift"
        m[0xE6] = "Right Alt"
        m[0xE7] = "Right Cmd"
        return m
    }()

    static func name(forHIDUsageCode code: Int) -> String {
        names[code] ?? "Key \(code)"
    }
}
