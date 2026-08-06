import GameController
import SwiftUI

/// Direct port of KeyBindingsActivity.kt: physical key binding per button,
/// split into GBA_BUTTONS (every stream type with any buttons at all
/// understands these) and EXT_BUTTONS/EXT_BUTTONS_LIMITED (only a
/// hasButtonsMode session -- Azahar's N3DS_BOTTOM_SCREEN today -- reads
/// these), plus a real, independent game-controller binding for the same
/// button set (its own "Gamepad bindings" section below) -- a keyboard
/// binding and a controller binding for the same GBA button are both
/// active at once during a session, not alternatives to each other (see
/// PlayerViewModel's own MARK on physical key/controller input). Two
/// completely separate capture mechanisms (KeyCaptureView vs.
/// ControllerCaptureView) since GameController framework has no
/// relationship to UIPress/UIKit's responder chain that KeyCaptureView
/// relies on at all -- see either file's own comment.
private enum BindTarget: Identifiable {
    case gba(GbaButton)
    case ext(ExtButton)

    var id: String {
        switch self {
        case .gba(let b): return "gba:\(b.prefKey)"
        case .ext(let b): return "ext:\(b.prefKey)"
        }
    }

    var label: String {
        switch self {
        case .gba(let b): return b.label
        case .ext(let b): return b.label
        }
    }
}

struct KeyBindingsView: View {
    private let prefs = Prefs()
    // Not a direct GCController.controllers() query (that was this
    // screen's first cut) -- see ControllerObserver's own comment: it's
    // neither reactive to a controller connecting/disconnecting while this
    // screen is already open, nor reliably populated for an
    // already-paired controller without an explicit discovery kick.
    @StateObject private var controllerObserver = ControllerObserver()
    @State private var pendingKeyTarget: BindTarget?
    @State private var pendingControllerTarget: BindTarget?
    // TEMPORARY diagnostic (2026-08-06) -- see ControllerCaptureView's own
    // comment on why this is surfaced directly in the UI instead of a log.
    @State private var controllerDebugInfo: String = ""
    // Prefs (UserDefaults) isn't @Published -- SwiftUI only re-evaluates a
    // View's body when one of its own @State properties changes, so a
    // plain prefs.clear...Binding() call from a Clear button's action
    // wouldn't otherwise refresh the row's displayed text at all. Toggled
    // after every mutation (either Clear button, and both sheets' capture
    // callbacks below) purely to force that re-evaluation; its value is
    // never read.
    @State private var refreshToken = false

    var body: some View {
        List {
            Section(LocaleHelper.string("key_bindings_standard_section", prefs: prefs)) {
                ForEach(GBA_BUTTONS) { button in
                    keyBindingRow(target: .gba(button), bindingText: describeKeyBinding(for: button))
                }
            }

            Section {
                ForEach(EXT_BUTTONS + EXT_BUTTONS_LIMITED) { button in
                    keyBindingRow(target: .ext(button), bindingText: describeKeyBinding(for: button))
                }
            } header: {
                Text(LocaleHelper.string("key_bindings_extended_section", prefs: prefs))
            } footer: {
                Text(LocaleHelper.string("key_bindings_extended_hint", prefs: prefs))
            }

            Section {
                if controllerObserver.controllers.isEmpty {
                    Text(LocaleHelper.string("key_bindings_no_gamepad", prefs: prefs))
                        .foregroundStyle(.secondary)
                }
                ForEach(GBA_BUTTONS) { button in
                    controllerBindingRow(target: .gba(button), bindingText: describeControllerBinding(for: button))
                }
                ForEach(EXT_BUTTONS + EXT_BUTTONS_LIMITED) { button in
                    controllerBindingRow(target: .ext(button), bindingText: describeControllerBinding(for: button))
                }
            } header: {
                Text(LocaleHelper.string("key_bindings_gamepad_section", prefs: prefs))
            }
        }
        .navigationTitle(LocaleHelper.string("settings_key_bindings", prefs: prefs))
        .sheet(item: $pendingKeyTarget) { target in
            VStack(spacing: 16) {
                Text(LocaleHelper.string("settings_press_key", prefs: prefs))
                    .font(.title2)
                Text(target.label)
                    .foregroundStyle(.secondary)
                // Invisible capture surface -- the prompt above is what's
                // actually shown, this just needs to be in the view tree
                // to become first responder and see the next press.
                KeyCaptureView { code in
                    switch target {
                    case .gba(let button): prefs.setKeyBinding(code, for: button)
                    case .ext(let button): prefs.setKeyBinding(code, for: button)
                    }
                    pendingKeyTarget = nil
                    refreshToken.toggle()
                }
                .frame(width: 1, height: 1)
            }
            .padding()
            .presentationDetents([.fraction(0.3)])
        }
        .sheet(item: $pendingControllerTarget) { target in
            VStack(spacing: 16) {
                Text(LocaleHelper.string("settings_press_key", prefs: prefs))
                    .font(.title2)
                Text(target.label)
                    .foregroundStyle(.secondary)
                if controllerObserver.controllers.isEmpty {
                    Text(LocaleHelper.string("key_bindings_no_gamepad", prefs: prefs))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                // TEMPORARY diagnostic (2026-08-06) -- see
                // ControllerCaptureView's own comment. Real, visible text
                // instead of the 1x1-invisible frame this had before, so
                // the live GCExtendedGamepad state is actually readable on
                // screen while this sheet is up. Explicit minHeight: a bare
                // ScrollView inside a VStack has no intrinsic height of its
                // own to lay out with -- without this, the VStack was
                // sizing it down to effectively nothing, making the text
                // invisible regardless of whether it was actually updating
                // underneath (confirmed by the user's own report: no text
                // at all, not even the "(waiting...)" placeholder that's
                // always present from the very first frame).
                ScrollView {
                    Text(controllerDebugInfo.isEmpty ? "(waiting for controller state...)" : controllerDebugInfo)
                        .font(.system(.caption, design: .monospaced))
                        .foregroundStyle(.secondary)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(.horizontal)
                }
                .frame(minHeight: 180, maxHeight: 260)
                .background(.quaternary.opacity(0.3))
                ControllerCaptureView(debugInfo: $controllerDebugInfo) { element in
                    switch target {
                    case .gba(let button): prefs.setControllerBinding(element, for: button)
                    case .ext(let button): prefs.setControllerBinding(element, for: button)
                    }
                    pendingControllerTarget = nil
                    refreshToken.toggle()
                }
                .frame(width: 1, height: 1)
            }
            .padding()
            .presentationDetents([.medium, .large])
        }
    }

    @ViewBuilder
    private func keyBindingRow(target: BindTarget, bindingText: String) -> some View {
        HStack {
            Text(target.label)
            Spacer()
            Text(bindingText)
                .foregroundStyle(.secondary)
            Button(LocaleHelper.string("settings_bind", prefs: prefs)) {
                pendingKeyTarget = target
            }
            .buttonStyle(.bordered)
            Button(LocaleHelper.string("settings_clear", prefs: prefs)) {
                switch target {
                case .gba(let button): prefs.clearKeyBinding(for: button)
                case .ext(let button): prefs.clearKeyBinding(for: button)
                }
                refreshToken.toggle()
            }
            .buttonStyle(.bordered)
        }
    }

    @ViewBuilder
    private func controllerBindingRow(target: BindTarget, bindingText: String) -> some View {
        HStack {
            Text(target.label)
            Spacer()
            Text(bindingText)
                .foregroundStyle(.secondary)
            Button(LocaleHelper.string("settings_bind", prefs: prefs)) {
                pendingControllerTarget = target
            }
            .buttonStyle(.bordered)
            .disabled(controllerObserver.controllers.isEmpty)
            Button(LocaleHelper.string("settings_clear", prefs: prefs)) {
                switch target {
                case .gba(let button): prefs.clearControllerBinding(for: button)
                case .ext(let button): prefs.clearControllerBinding(for: button)
                }
                refreshToken.toggle()
            }
            .buttonStyle(.bordered)
        }
    }

    private func describeKeyBinding(for button: GbaButton) -> String {
        guard let code = prefs.keyBinding(for: button) else {
            return LocaleHelper.string("settings_unbound", prefs: prefs)
        }
        return KeyBindingName.name(forHIDUsageCode: code)
    }

    private func describeKeyBinding(for button: ExtButton) -> String {
        guard let code = prefs.keyBinding(for: button) else {
            return LocaleHelper.string("settings_unbound", prefs: prefs)
        }
        return KeyBindingName.name(forHIDUsageCode: code)
    }

    private func describeControllerBinding(for button: GbaButton) -> String {
        guard let element = prefs.controllerBinding(for: button) else {
            return LocaleHelper.string("settings_unbound", prefs: prefs)
        }
        return element.displayName
    }

    private func describeControllerBinding(for button: ExtButton) -> String {
        guard let element = prefs.controllerBinding(for: button) else {
            return LocaleHelper.string("settings_unbound", prefs: prefs)
        }
        return element.displayName
    }
}

#Preview {
    NavigationStack { KeyBindingsView() }
}
