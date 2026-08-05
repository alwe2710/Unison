import GameController

/// Real game-controller (MFi/Bluetooth/USB, GameController framework) input
/// during a PlayerView session -- the piece PlayerKeyInputView's own
/// comment (and KeyCaptureView's, and this session's real-device testing
/// feedback) documents as missing: iOS has no unified key-event story
/// covering both a hardware keyboard and a controller the way Android's
/// KeyEvent does, so this is a completely separate input path from
/// PlayerViewModel.handlePhysicalKey(), not a variation of it.
///
/// Fixed, sensible default mapping -- no per-button rebind UI like the
/// keyboard's own KeyBindingsView. A `GCExtendedGamepad` is a fixed,
/// well-known button/dpad/stick set (not arbitrary input the way a
/// keyboard's HID codes are), so a single reasonable default is enough
/// for v1: matches how every other client's own physical D-pad/buttons
/// already have one fixed mapping with no rebind screen either (3DS/
/// Switch's physical buttons already line up with the GBA's own layout,
/// see clients/3ds/source/gba_buttons.hpp's own comment). A later pass
/// could add per-element rebinding if that turns out to matter in
/// practice.
///
/// Mapping (dpad/buttonA/buttonB/leftShoulder/rightShoulder/buttonMenu/
/// buttonOptions cover every plain gba_buttons session already; buttonX/Y,
/// the triggers, and both thumbsticks only apply to a hasButtons/hasSticks
/// session, where there's a real place on the wire for them):
///   D-pad          -> Up/Down/Left/Right
///   A / B          -> A / B
///   Left/Right shoulder -> L / R
///   Menu button    -> Start
///   Options button (if present -- not every MFi controller has one) -> Select
///   X / Y          -> Ext X / Y (hasButtons only)
///   Left/Right trigger  -> Ext ZL / ZR (hasSticks only)
///   Left/Right thumbstick -> the stream's own two analog sticks (hasSticks only)
final class ControllerInputHandler {
    private weak var viewModel: PlayerViewModel?
    private var observers: [NSObjectProtocol] = []

    init(viewModel: PlayerViewModel) {
        self.viewModel = viewModel
        let center = NotificationCenter.default
        observers.append(center.addObserver(forName: .GCControllerDidConnect, object: nil, queue: .main) { [weak self] note in
            guard let controller = note.object as? GCController else { return }
            self?.attach(controller)
        })
        // A controller already connected before this screen appeared
        // (common -- most people pair one once, then just launch games)
        // never fires GCControllerDidConnect again on its own, so the
        // existing list needs its own explicit pass here too.
        for controller in GCController.controllers() {
            attach(controller)
        }
    }

    deinit {
        let center = NotificationCenter.default
        for observer in observers {
            center.removeObserver(observer)
        }
    }

    private func attach(_ controller: GCController) {
        guard let gamepad = controller.extendedGamepad else { return }
        // GCExtendedGamepad's valueChangedHandler fires on
        // controller.handlerQueue, .main by default (Apple's own
        // documented default) -- not set explicitly here, since the
        // default is already exactly what's needed to touch
        // PlayerViewModel's @Published state safely.
        gamepad.valueChangedHandler = { [weak self] pad, _ in
            self?.sync(pad)
        }
    }

    /// Re-reads and re-sends the controller's *entire* current state on
    /// every single element change, rather than translating just the one
    /// element that fired -- simpler (one code path, not ten separate
    /// element handlers), and cheap enough at real controller input rates.
    /// Mirrors PlayerViewModel.setExtButton()/sendCombined()'s own "resend
    /// everything together" contract, so this isn't adding any new
    /// per-change cost that wasn't already happening for touch/stick
    /// input.
    private func sync(_ gamepad: GCExtendedGamepad) {
        guard let viewModel else { return }

        if viewModel.touchInput, viewModel.hasButtons {
            viewModel.setExtButton(bit: UInt32(ExtButtonBit.A), pressed: gamepad.buttonA.isPressed)
            viewModel.setExtButton(bit: UInt32(ExtButtonBit.B), pressed: gamepad.buttonB.isPressed)
            viewModel.setExtButton(bit: UInt32(ExtButtonBit.X), pressed: gamepad.buttonX.isPressed)
            viewModel.setExtButton(bit: UInt32(ExtButtonBit.Y), pressed: gamepad.buttonY.isPressed)
            viewModel.setExtButton(bit: UInt32(ExtButtonBit.L), pressed: gamepad.leftShoulder.isPressed)
            viewModel.setExtButton(bit: UInt32(ExtButtonBit.R), pressed: gamepad.rightShoulder.isPressed)
            viewModel.setExtButton(bit: UInt32(ExtButtonBit.START), pressed: gamepad.buttonMenu.isPressed)
            viewModel.setExtButton(bit: UInt32(ExtButtonBit.SELECT), pressed: gamepad.buttonOptions?.isPressed ?? false)
            viewModel.setExtButton(bit: UInt32(ExtButtonBit.UP), pressed: gamepad.dpad.up.isPressed)
            viewModel.setExtButton(bit: UInt32(ExtButtonBit.DOWN), pressed: gamepad.dpad.down.isPressed)
            viewModel.setExtButton(bit: UInt32(ExtButtonBit.LEFT), pressed: gamepad.dpad.left.isPressed)
            viewModel.setExtButton(bit: UInt32(ExtButtonBit.RIGHT), pressed: gamepad.dpad.right.isPressed)
            if viewModel.hasSticks {
                viewModel.setExtButton(bit: UInt32(ExtButtonBit.ZL), pressed: gamepad.leftTrigger.isPressed)
                viewModel.setExtButton(bit: UInt32(ExtButtonBit.ZR), pressed: gamepad.rightTrigger.isPressed)
                viewModel.setLeftStick(x: Self.axis(gamepad.leftThumbstick.xAxis.value),
                                        y: Self.axis(gamepad.leftThumbstick.yAxis.value))
                viewModel.setRightStick(x: Self.axis(gamepad.rightThumbstick.xAxis.value),
                                         y: Self.axis(gamepad.rightThumbstick.yAxis.value))
            }
        } else {
            viewModel.setButton(bit: GbaKey.A, pressed: gamepad.buttonA.isPressed)
            viewModel.setButton(bit: GbaKey.B, pressed: gamepad.buttonB.isPressed)
            viewModel.setButton(bit: GbaKey.L, pressed: gamepad.leftShoulder.isPressed)
            viewModel.setButton(bit: GbaKey.R, pressed: gamepad.rightShoulder.isPressed)
            viewModel.setButton(bit: GbaKey.START, pressed: gamepad.buttonMenu.isPressed)
            viewModel.setButton(bit: GbaKey.SELECT, pressed: gamepad.buttonOptions?.isPressed ?? false)
            viewModel.setButton(bit: GbaKey.UP, pressed: gamepad.dpad.up.isPressed)
            viewModel.setButton(bit: GbaKey.DOWN, pressed: gamepad.dpad.down.isPressed)
            viewModel.setButton(bit: GbaKey.LEFT, pressed: gamepad.dpad.left.isPressed)
            viewModel.setButton(bit: GbaKey.RIGHT, pressed: gamepad.dpad.right.isPressed)
        }
    }

    /// GCControllerAxisInput.value is already -1.0...1.0 with "up"/"right"
    /// positive -- the same convention Stick's own onChange callback (and
    /// the wire's unison_extended_input fields) use, so no y-sign flip
    /// needed here unlike Stick's own drag-gesture math (screen-down is
    /// positive there; a physical thumbstick's own value never had that
    /// problem to begin with).
    private static func axis(_ value: Float) -> Int16 {
        Int16((value * 32767).rounded())
    }
}
