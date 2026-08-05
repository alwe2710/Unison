import SwiftUI

/// Direct port of KeyBindingsActivity.kt: physical key binding per button,
/// split into GBA_BUTTONS (every stream type with any buttons at all
/// understands these) and EXT_BUTTONS/EXT_BUTTONS_LIMITED (only a
/// hasButtonsMode session -- Azahar's N3DS_BOTTOM_SCREEN today -- reads
/// these). This screen itself (and KeyCaptureView, its capture-the-next-
/// key mechanism) is hardware-keyboard-only -- see KeyCaptureView's own
/// comment for why a game controller's *rebinding* isn't part of this UI,
/// even though a controller's input does work during an actual session via
/// a separate, fixed default mapping (ControllerInputHandler.swift).
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
    @State private var pendingTarget: BindTarget?
    // Prefs (UserDefaults) isn't @Published -- SwiftUI only re-evaluates a
    // View's body when one of its own @State properties changes, so a
    // plain prefs.clearKeyBinding() call from the Clear button's action
    // wouldn't otherwise refresh the row's displayed text at all. Toggled
    // after every mutation (Clear here, and the sheet's onKey callback
    // below) purely to force that re-evaluation; its value is never read.
    @State private var refreshToken = false

    var body: some View {
        List {
            Section(LocaleHelper.string("key_bindings_standard_section", prefs: prefs)) {
                ForEach(GBA_BUTTONS) { button in
                    bindingRow(target: .gba(button), bindingText: describeBinding(for: button))
                }
            }

            Section {
                ForEach(EXT_BUTTONS + EXT_BUTTONS_LIMITED) { button in
                    bindingRow(target: .ext(button), bindingText: describeBinding(for: button))
                }
            } header: {
                Text(LocaleHelper.string("key_bindings_extended_section", prefs: prefs))
            } footer: {
                Text(LocaleHelper.string("key_bindings_extended_hint", prefs: prefs))
            }
        }
        .navigationTitle(LocaleHelper.string("settings_key_bindings", prefs: prefs))
        .sheet(item: $pendingTarget) { target in
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
                    pendingTarget = nil
                }
                .frame(width: 1, height: 1)
            }
            .padding()
            .presentationDetents([.fraction(0.3)])
        }
    }

    @ViewBuilder
    private func bindingRow(target: BindTarget, bindingText: String) -> some View {
        HStack {
            Text(target.label)
            Spacer()
            Text(bindingText)
                .foregroundStyle(.secondary)
            Button(LocaleHelper.string("settings_bind", prefs: prefs)) {
                pendingTarget = target
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

    private func describeBinding(for button: GbaButton) -> String {
        guard let code = prefs.keyBinding(for: button) else {
            return LocaleHelper.string("settings_unbound", prefs: prefs)
        }
        return KeyBindingName.name(forHIDUsageCode: code)
    }

    private func describeBinding(for button: ExtButton) -> String {
        guard let code = prefs.keyBinding(for: button) else {
            return LocaleHelper.string("settings_unbound", prefs: prefs)
        }
        return KeyBindingName.name(forHIDUsageCode: code)
    }
}

#Preview {
    NavigationStack { KeyBindingsView() }
}
