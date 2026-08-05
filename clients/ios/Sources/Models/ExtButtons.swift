import Foundation

/// One entry per unison_extended_input control (protocol.h's
/// unison_button_bit bits, plus eight synthetic stick-direction entries),
/// shared between PlayerView (physical key handling for a hasButtonsMode
/// session) and KeyBindingsView, mirroring GbaButton's own role for the
/// plain gba_buttons encoding. Direct port of clients/android/.../
/// ExtButtons.kt -- see that file's own comment for the full rationale.
enum ExtInputKind {
    case button
    case stickLUp, stickLDown, stickLLeft, stickLRight
    case stickRUp, stickRDown, stickRLeft, stickRRight
}

struct ExtButton: Identifiable {
    let label: String
    let kind: ExtInputKind
    let bit: Int
    let prefKey: String
    var id: String { prefKey }
}

/// Only the hasButtonsMode controls with no Standard-Tasten (GBA_BUTTONS)
/// counterpart -- X/Y are real 3DS buttons the GBA overlay has no
/// equivalent for.
let EXT_BUTTONS: [ExtButton] = [
    ExtButton(label: "X", kind: .button, bit: ExtButtonBit.X, prefKey: "X"),
    ExtButton(label: "Y", kind: .button, bit: ExtButtonBit.Y, prefKey: "Y"),
]

/// GbaButton.prefKey -> the unison_button_bit it also represents in a
/// hasButtonsMode session, for the subset both encodings share.
let GBA_PREFKEY_TO_EXT_BUTTON_BIT: [String: Int] = [
    "UP": ExtButtonBit.UP,
    "DOWN": ExtButtonBit.DOWN,
    "LEFT": ExtButtonBit.LEFT,
    "RIGHT": ExtButtonBit.RIGHT,
    "SELECT": ExtButtonBit.SELECT,
    "START": ExtButtonBit.START,
    "L": ExtButtonBit.L,
    "R": ExtButtonBit.R,
    "A": ExtButtonBit.A,
    "B": ExtButtonBit.B,
]

/// ZL/ZR and both sticks' directions -- only meaningful on stream types
/// whose console actually has them (Cemu's WIIU_GAMEPAD today).
let EXT_BUTTONS_LIMITED: [ExtButton] = [
    ExtButton(label: "ZL", kind: .button, bit: ExtButtonBit.ZL, prefKey: "ZL"),
    ExtButton(label: "ZR", kind: .button, bit: ExtButtonBit.ZR, prefKey: "ZR"),
    ExtButton(label: "Stick L up", kind: .stickLUp, bit: 0, prefKey: "STICK_L_UP"),
    ExtButton(label: "Stick L down", kind: .stickLDown, bit: 0, prefKey: "STICK_L_DOWN"),
    ExtButton(label: "Stick L left", kind: .stickLLeft, bit: 0, prefKey: "STICK_L_LEFT"),
    ExtButton(label: "Stick L right", kind: .stickLRight, bit: 0, prefKey: "STICK_L_RIGHT"),
    ExtButton(label: "Stick R up", kind: .stickRUp, bit: 0, prefKey: "STICK_R_UP"),
    ExtButton(label: "Stick R down", kind: .stickRDown, bit: 0, prefKey: "STICK_R_DOWN"),
    ExtButton(label: "Stick R left", kind: .stickRLeft, bit: 0, prefKey: "STICK_R_LEFT"),
    ExtButton(label: "Stick R right", kind: .stickRRight, bit: 0, prefKey: "STICK_R_RIGHT"),
]
