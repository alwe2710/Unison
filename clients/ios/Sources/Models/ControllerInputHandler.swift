import GameController

/// Real game-controller (MFi/Bluetooth/USB, GameController framework) input
/// during a PlayerView session -- a completely separate input path from
/// PlayerViewModel.handlePhysicalKey() (the keyboard's own), not a
/// variation of it: iOS has no unified key-event story covering both a
/// hardware keyboard and a controller the way Android's KeyEvent does.
///
/// Prefs-driven, exactly mirroring PlayerViewModel.handlePhysicalKey()'s
/// dispatch shape (Prefs.controllerBindingsByElement()/
/// extControllerBindingsByElement()/sharedExtButtonBitsByControllerElement(),
/// same three-dictionary pattern as the keyboard's own
/// keyBindingsByKeyCode()/etc.) -- KeyBindingsView's new "Gamepad bindings"
/// section is what actually configures these; nothing here hardcodes which
/// physical button does what. An unbound button/dpad-direction does
/// nothing, same as an unbound keyboard key -- consistent with that
/// screen's own existing convention rather than silently falling back to
/// some fixed default a user configured *around* instead of *out of*.
///
/// Analog thumbsticks are the one exception: their raw x/y values always
/// drive the session's own two analog sticks directly (when hasSticks)
/// regardless of any binding -- that's not really a "binding" question,
/// it's just what a real thumbstick naturally does, the same way a
/// keyboard (which has no analog axes at all) never had an equivalent
/// concept to begin with. A thumbstick direction can *additionally* be
/// bound as a discrete digital element (e.g. "L-Stick Up" bound to a plain
/// GBA button) without that binding replacing the stick's own analog feed.
final class ControllerInputHandler {
    private weak var viewModel: PlayerViewModel?
    private var observers: [NSObjectProtocol] = []
    private let prefs = Prefs()

    private lazy var elementToBit = prefs.controllerBindingsByElement()
    private lazy var elementToExtButton = prefs.extControllerBindingsByElement()
    private lazy var elementToExtBitFromGba = prefs.sharedExtButtonBitsByControllerElement()

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
        // Real-device gap, reported directly: GCController.controllers()
        // doesn't reliably reflect an already-OS-paired-and-connected
        // controller until the app has explicitly kicked off discovery at
        // least once -- see ControllerObserver's own comment (this class
        // doesn't share that one's SwiftUI-observability concern, but has
        // the identical underlying GameController-framework gotcha). Kicked
        // *after* the pass above, not before -- if the controller is
        // already enumerable this call changes nothing observable; if it
        // isn't yet, GCControllerDidConnect (already observed above) is
        // what picks it up once discovery actually finds it.
        GCController.startWirelessControllerDiscovery {}
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

        // Same condition as PlayerViewModel.handlePhysicalKey()'s own
        // `if hasButtons, ...` gate (hasButtons alone, not touchInput &&
        // hasButtons) -- the protocol only ever reports hasButtons=true
        // for a touch-capable session to begin with, so the two are
        // equivalent in practice; matching the exact same condition here
        // keeps both physical-input paths visibly consistent instead of
        // looking like they disagree about something.
        let extendedSession = viewModel.hasButtons

        for element in ControllerElement.allCases {
            let pressed = Self.isPressed(element, on: gamepad)
            dispatch(element, pressed: pressed, extendedSession: extendedSession)
        }

        if extendedSession, viewModel.hasSticks {
            viewModel.setLeftStick(x: Self.axis(gamepad.leftThumbstick.xAxis.value),
                                    y: Self.axis(gamepad.leftThumbstick.yAxis.value))
            viewModel.setRightStick(x: Self.axis(gamepad.rightThumbstick.xAxis.value),
                                     y: Self.axis(gamepad.rightThumbstick.yAxis.value))
        }
    }

    /// Direct port of PlayerViewModel.handlePhysicalKey()'s own dispatch
    /// shape, just keyed by ControllerElement instead of a keyboard HID
    /// code, and with no "was this handled" return value to fall through
    /// with (a controller press has no system responder chain to fall
    /// through to at all).
    private func dispatch(_ element: ControllerElement, pressed: Bool, extendedSession: Bool) {
        guard let viewModel else { return }

        if extendedSession {
            if let button = elementToExtButton[element] {
                if button.kind == .button {
                    viewModel.setExtButton(bit: UInt32(button.bit), pressed: pressed)
                }
                // Stick-direction ExtButton kinds bound to a discrete
                // controller element (e.g. a face button standing in for
                // "L-Stick Up") aren't handled here -- PlayerViewModel's
                // own digital-stick accumulation (extKeyStick...) is
                // private to its keyboard path, and duplicating it for the
                // rare case of binding a *button* to a *stick direction* on
                // a device that already has real analog sticks isn't worth
                // the complexity this pass. The element's own binding
                // dropdown still accepts it (consistent with the keyboard
                // side), it's just a no-op for now if picked.
            }
            if let bit = elementToExtBitFromGba[element] {
                viewModel.setExtButton(bit: UInt32(bit), pressed: pressed)
            }
        } else if let bit = elementToBit[element] {
            viewModel.setButton(bit: bit, pressed: pressed)
        }
    }

    private static func isPressed(_ element: ControllerElement, on pad: GCExtendedGamepad) -> Bool {
        let threshold: Float = 0.6
        switch element {
        case .buttonA: return pad.buttonA.isPressed
        case .buttonB: return pad.buttonB.isPressed
        case .buttonX: return pad.buttonX.isPressed
        case .buttonY: return pad.buttonY.isPressed
        case .leftShoulder: return pad.leftShoulder.isPressed
        case .rightShoulder: return pad.rightShoulder.isPressed
        case .leftTrigger: return pad.leftTrigger.isPressed
        case .rightTrigger: return pad.rightTrigger.isPressed
        case .buttonMenu: return pad.buttonMenu.isPressed
        case .buttonOptions: return pad.buttonOptions?.isPressed ?? false
        case .dpadUp: return pad.dpad.up.isPressed
        case .dpadDown: return pad.dpad.down.isPressed
        case .dpadLeft: return pad.dpad.left.isPressed
        case .dpadRight: return pad.dpad.right.isPressed
        case .leftThumbstickUp: return pad.leftThumbstick.yAxis.value > threshold
        case .leftThumbstickDown: return pad.leftThumbstick.yAxis.value < -threshold
        case .leftThumbstickLeft: return pad.leftThumbstick.xAxis.value < -threshold
        case .leftThumbstickRight: return pad.leftThumbstick.xAxis.value > threshold
        case .rightThumbstickUp: return pad.rightThumbstick.yAxis.value > threshold
        case .rightThumbstickDown: return pad.rightThumbstick.yAxis.value < -threshold
        case .rightThumbstickLeft: return pad.rightThumbstick.xAxis.value < -threshold
        case .rightThumbstickRight: return pad.rightThumbstick.xAxis.value > threshold
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
