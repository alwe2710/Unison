import SwiftUI
import UIKit

/// Intercepts the next physical-keyboard press, calling `onKey` with its
/// UIKeyboardHIDUsage raw value (same value space Prefs.setKeyBinding(_:for:)
/// stores, see that file's own comment). SwiftUI's own `.onKeyPress`
/// modifier (iOS 17+) only exposes a semantic KeyEquivalent/character, not
/// a raw HID code -- UIKit's pressesBegan(_:with:) is the API that
/// actually gives one, hence this small UIViewRepresentable bridge rather
/// than pure SwiftUI.
///
/// Real, documented gap (not silently faked): this only ever sees a
/// connected *hardware keyboard* press. A game controller's buttons go
/// through an entirely separate API (GameController framework's
/// GCController/GCExtendedGamepad, not UIPress/UIKey at all) -- Android's
/// KeyBindingsActivity.kt binds both uniformly because Android's KeyEvent
/// system happens to cover Bluetooth/USB gamepads too, but iOS has no such
/// unification. Controller binding support isn't implemented here.
struct KeyCaptureView: UIViewRepresentable {
    let onKey: (Int) -> Void

    func makeUIView(context: Context) -> CaptureUIView {
        let view = CaptureUIView()
        view.onKey = onKey
        return view
    }

    func updateUIView(_ uiView: CaptureUIView, context: Context) {
        uiView.onKey = onKey
        if uiView.window != nil {
            uiView.becomeFirstResponder()
        }
    }

    final class CaptureUIView: UIView {
        var onKey: ((Int) -> Void)?

        override var canBecomeFirstResponder: Bool { true }

        override func didMoveToWindow() {
            super.didMoveToWindow()
            if window != nil {
                becomeFirstResponder()
            }
        }

        override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
            for press in presses {
                if let key = press.key {
                    onKey?(Int(key.keyCode.rawValue))
                    return
                }
            }
            super.pressesBegan(presses, with: event)
        }
    }
}
