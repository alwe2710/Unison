import Foundation

/// One entry per GCExtendedGamepad element this app can bind a GBA/Ext
/// button to -- a stable, Codable-friendly (String rawValue) identifier for
/// a specific physical controller input, since a real GCControllerElement
/// instance is neither Codable nor stable across controller (re)connections
/// (a fresh GCController/GCExtendedGamepad instance is created each time a
/// controller connects, even for the *same* physical controller) -- Prefs
/// stores this rawValue string, never anything from GameController itself.
/// Same role KeyBindingName's HID-usage-code space has for a keyboard
/// binding, just for the controller's fixed, well-known element set
/// instead of an arbitrary keyboard scan code.
enum ControllerElement: String, CaseIterable, Identifiable {
    case buttonA, buttonB, buttonX, buttonY
    case leftShoulder, rightShoulder, leftTrigger, rightTrigger
    case buttonMenu, buttonOptions
    case dpadUp, dpadDown, dpadLeft, dpadRight
    case leftThumbstickUp, leftThumbstickDown, leftThumbstickLeft, leftThumbstickRight
    case rightThumbstickUp, rightThumbstickDown, rightThumbstickLeft, rightThumbstickRight

    var id: String { rawValue }

    /// Display name for KeyBindingsView's row text -- same role
    /// KeyBindingName.name(forHIDUsageCode:) has for a keyboard binding.
    var displayName: String {
        switch self {
        case .buttonA: return "A"
        case .buttonB: return "B"
        case .buttonX: return "X"
        case .buttonY: return "Y"
        case .leftShoulder: return "L1"
        case .rightShoulder: return "R1"
        case .leftTrigger: return "L2"
        case .rightTrigger: return "R2"
        case .buttonMenu: return "Menu"
        case .buttonOptions: return "Options"
        case .dpadUp: return "D-Pad ↑"
        case .dpadDown: return "D-Pad ↓"
        case .dpadLeft: return "D-Pad ←"
        case .dpadRight: return "D-Pad →"
        case .leftThumbstickUp: return "L-Stick ↑"
        case .leftThumbstickDown: return "L-Stick ↓"
        case .leftThumbstickLeft: return "L-Stick ←"
        case .leftThumbstickRight: return "L-Stick →"
        case .rightThumbstickUp: return "R-Stick ↑"
        case .rightThumbstickDown: return "R-Stick ↓"
        case .rightThumbstickLeft: return "R-Stick ←"
        case .rightThumbstickRight: return "R-Stick →"
        }
    }
}
