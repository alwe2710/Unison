import Foundation

/// One entry per physical GBA button, shared between PlayerView (touch row
/// + physical keyboard handling) and SettingsView (key binding list) so
/// both stay in sync with a single source of truth. Mirrors
/// clients/android/.../GbaButtons.kt exactly (see that file's own comment).
struct GbaButton: Identifiable {
    let label: String
    let bit: Int
    let prefKey: String
    var id: String { prefKey }
}

let GBA_BUTTONS: [GbaButton] = [
    GbaButton(label: "Up", bit: GbaKey.UP, prefKey: "UP"),
    GbaButton(label: "Down", bit: GbaKey.DOWN, prefKey: "DOWN"),
    GbaButton(label: "Left", bit: GbaKey.LEFT, prefKey: "LEFT"),
    GbaButton(label: "Right", bit: GbaKey.RIGHT, prefKey: "RIGHT"),
    GbaButton(label: "Select", bit: GbaKey.SELECT, prefKey: "SELECT"),
    GbaButton(label: "Start", bit: GbaKey.START, prefKey: "START"),
    GbaButton(label: "L", bit: GbaKey.L, prefKey: "L"),
    GbaButton(label: "R", bit: GbaKey.R, prefKey: "R"),
    GbaButton(label: "B", bit: GbaKey.B, prefKey: "B"),
    GbaButton(label: "A", bit: GbaKey.A, prefKey: "A"),
]
