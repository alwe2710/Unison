import GameController
import SwiftUI

/// Same "capture the next physical input" role as KeyCaptureView, but for a
/// game controller button/dpad-direction/thumbstick-direction instead of a
/// hardware-keyboard key -- GameController framework has no relationship to
/// UIPress/UIKit's responder chain at all, so this is a completely separate
/// mechanism, not a variation of KeyCaptureView's UIViewRepresentable. Pure
/// SwiftUI (no UIKit bridge needed): sets valueChangedHandler on every
/// currently-connected extended gamepad for as long as this view is on
/// screen, and reports the first element that crosses the "pressed" test
/// below via `onElement`.
///
/// `debugInfo` (TEMPORARY, 2026-08-06): two real fix attempts (a fixed
/// default mapping, then a proper Prefs-driven binding UI, then a
/// detection/reactivity fix) still didn't resolve "controller presses
/// aren't registered" on real hardware -- rather than guess a fourth time,
/// this surfaces the actual live GCExtendedGamepad state directly in the
/// capture sheet's own UI (controller count/name/extendedGamepad presence,
/// then a live raw button/axis dump on every valueChangedHandler firing)
/// so the user can just read their screen and report back what it says,
/// the same "get a real look instead of guessing again" approach that
/// resolved the BeaconListener and SettingsViewUITests sagas earlier this
/// session (there via CI log output; here via on-screen text, since there's
/// no console log to read from a real device in this environment). Remove
/// once the actual cause is confirmed and fixed.
///
/// Not thread-safety-audited for two controllers pressed at the *exact*
/// same instant (an edge case not worth over-engineering for a one-shot
/// "press a button to bind it" flow) -- whichever handler fires first wins,
/// same as a real keyboard press already can't be "two keys at once"
/// meaningfully for KeyCaptureView either.
struct ControllerCaptureView: View {
    // debugInfo declared before onElement so the call site can use
    // trailing-closure syntax for onElement (Swift's trailing-closure sugar
    // only applies to a call's *last* parameter, which the compiler
    // determines from declaration order for this struct's auto-generated
    // memberwise initializer).
    @Binding var debugInfo: String
    let onElement: (ControllerElement) -> Void

    var body: some View {
        Color.clear
            .onAppear { attachAll() }
            .onDisappear { detachAll() }
    }

    private func attachAll() {
        // Defense-in-depth: KeyBindingsView's own ControllerObserver
        // already kicks this off when that screen appears, but this sheet
        // shouldn't depend on that ordering to work correctly on its own.
        GCController.startWirelessControllerDiscovery {}

        let controllers = GCController.controllers()
        if controllers.isEmpty {
            debugInfo = "GCController.controllers(): empty"
            return
        }
        var lines = ["\(controllers.count) controller(s):"]
        for controller in controllers {
            lines.append("- \(controller.vendorName ?? "(no vendorName)") "
                + "[\(controller.productCategory)] "
                + "extendedGamepad: \(controller.extendedGamepad != nil)")
        }
        debugInfo = lines.joined(separator: "\n")

        for controller in controllers {
            guard let gamepad = controller.extendedGamepad else { continue }
            gamepad.valueChangedHandler = { pad, element in
                debugInfo = "last change: \(element.localizedName ?? element.aliases.first ?? "?")\n"
                    + Self.rawSummary(pad)
                if let matched = Self.firstPressedElement(pad) {
                    onElement(matched)
                }
            }
        }
    }

    /// Restores GameController's own default (no handler) rather than
    /// leaving this transient capture handler installed once the binding
    /// sheet closes -- ControllerInputHandler (real gameplay input) sets
    /// its own handler fresh each time a PlayerView session starts, so
    /// this doesn't need to hand anything back, just not linger.
    private func detachAll() {
        for controller in GCController.controllers() {
            controller.extendedGamepad?.valueChangedHandler = nil
        }
    }

    /// Every element's raw current value, regardless of `firstPressedElement`'s
    /// own threshold/priority logic below -- if the real hardware sends
    /// anything at all, it shows up here even if firstPressedElement's own
    /// interpretation of it turns out to be wrong.
    private static func rawSummary(_ pad: GCExtendedGamepad) -> String {
        "A:\(pad.buttonA.isPressed) B:\(pad.buttonB.isPressed) X:\(pad.buttonX.isPressed) Y:\(pad.buttonY.isPressed)\n"
            + "L1:\(pad.leftShoulder.isPressed) R1:\(pad.rightShoulder.isPressed) "
            + "L2:\(pad.leftTrigger.value) R2:\(pad.rightTrigger.value)\n"
            + "Menu:\(pad.buttonMenu.isPressed) Options:\(pad.buttonOptions?.isPressed.description ?? "n/a")\n"
            + "dpad up/down/left/right: \(pad.dpad.up.isPressed)/\(pad.dpad.down.isPressed)/"
            + "\(pad.dpad.left.isPressed)/\(pad.dpad.right.isPressed)\n"
            + "L-stick: (\(pad.leftThumbstick.xAxis.value), \(pad.leftThumbstick.yAxis.value))\n"
            + "R-stick: (\(pad.rightThumbstick.xAxis.value), \(pad.rightThumbstick.yAxis.value))"
    }

    /// Checked in a fixed, deliberate order (buttons, then dpad, then
    /// sticks) -- a single physical press only ever expresses one binding
    /// intent, so once any element crosses the "pressed" threshold, every
    /// other currently-neutral element is irrelevant to this callback.
    /// 0.6 for the analog thumbstick threshold: comfortably past "just
    /// resting off-center" (real sticks rarely sit perfectly at 0) without
    /// requiring a full, uncomfortable end-of-travel push to register.
    private static func firstPressedElement(_ pad: GCExtendedGamepad) -> ControllerElement? {
        let threshold: Float = 0.6
        if pad.buttonA.isPressed { return .buttonA }
        if pad.buttonB.isPressed { return .buttonB }
        if pad.buttonX.isPressed { return .buttonX }
        if pad.buttonY.isPressed { return .buttonY }
        if pad.leftShoulder.isPressed { return .leftShoulder }
        if pad.rightShoulder.isPressed { return .rightShoulder }
        if pad.leftTrigger.isPressed { return .leftTrigger }
        if pad.rightTrigger.isPressed { return .rightTrigger }
        if pad.buttonMenu.isPressed { return .buttonMenu }
        if pad.buttonOptions?.isPressed == true { return .buttonOptions }
        if pad.dpad.up.isPressed { return .dpadUp }
        if pad.dpad.down.isPressed { return .dpadDown }
        if pad.dpad.left.isPressed { return .dpadLeft }
        if pad.dpad.right.isPressed { return .dpadRight }
        if pad.leftThumbstick.yAxis.value > threshold { return .leftThumbstickUp }
        if pad.leftThumbstick.yAxis.value < -threshold { return .leftThumbstickDown }
        if pad.leftThumbstick.xAxis.value < -threshold { return .leftThumbstickLeft }
        if pad.leftThumbstick.xAxis.value > threshold { return .leftThumbstickRight }
        if pad.rightThumbstick.yAxis.value > threshold { return .rightThumbstickUp }
        if pad.rightThumbstick.yAxis.value < -threshold { return .rightThumbstickDown }
        if pad.rightThumbstick.xAxis.value < -threshold { return .rightThumbstickLeft }
        if pad.rightThumbstick.xAxis.value > threshold { return .rightThumbstickRight }
        return nil
    }
}
