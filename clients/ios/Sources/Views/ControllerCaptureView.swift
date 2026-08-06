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
/// Not thread-safety-audited for two controllers pressed at the *exact*
/// same instant (an edge case not worth over-engineering for a one-shot
/// "press a button to bind it" flow) -- whichever handler fires first wins,
/// same as a real keyboard press already can't be "two keys at once"
/// meaningfully for KeyCaptureView either.
struct ControllerCaptureView: View {
    let onElement: (ControllerElement) -> Void

    var body: some View {
        Color.clear
            .onAppear { attachAll() }
            .onDisappear { detachAll() }
    }

    private func attachAll() {
        for controller in GCController.controllers() {
            guard let gamepad = controller.extendedGamepad else { continue }
            gamepad.valueChangedHandler = { pad, _ in
                if let element = Self.firstPressedElement(pad) {
                    onElement(element)
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
