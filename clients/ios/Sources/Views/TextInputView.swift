import SwiftUI
import UIKit

/// The server's own on-screen keyboard request, threaded through as a
/// fullScreenCover(item:) trigger -- see PlayerView.body's own wiring.
/// Identifiable (a fresh id per request) rather than a plain Bool flag so a
/// second request arriving while one is already showing (unlikely, but not
/// impossible if the server re-sends) still gets a fresh, correctly
/// pre-filled sheet instead of SwiftUI treating it as "still the same
/// presentation".
struct TextInputRequest: Identifiable {
    let id = UUID()
    let maxLength: Int32
    let initialText: String
}

/// iOS counterpart to TextInputActivity.kt -- the emulated console's own
/// on-screen software keyboard (e.g. Cemu's WiiU swkbd) is drawn as a
/// host-side UI overlay never captured by the video stream, so this stands
/// in for it entirely. A real, fullscreen cover (not a small alert/sheet)
/// for the same reason Android uses a full Activity rather than a Dialog:
/// this needs to feel like an ordinary full-screen text-entry moment, not a
/// popup fighting the video underneath for space.
///
/// A real TextField (not a custom UIViewRepresentable/UITextField, unlike
/// TextInputActivity.kt's own AndroidView(EditText) -- SwiftUI's TextField
/// already drives the system keyboard the normal way here; the Android
/// side's UIViewRepresentable detour was specifically to route around
/// Compose's TextField suppressing the IME's native fullscreen "extract
/// mode", which has no iOS equivalent to route around).
struct TextInputView: View {
    let maxLength: Int32
    let initialText: String
    let onSubmit: (Bool, String) -> Void

    @Environment(\.dismiss) private var dismiss
    @State private var text: String
    @FocusState private var focused: Bool
    // Guards onDisappear's fallback below from double-firing onSubmit --
    // Cancel/OK already call it directly (then dismiss()), so onDisappear
    // only needs to cover any *other* way this cover ends up gone (fullScreenCover
    // itself has no built-in swipe-to-dismiss the way .sheet does, but this
    // stays defensive rather than assuming that never changes).
    @State private var responded = false
    private let prefs = Prefs()

    init(maxLength: Int32, initialText: String, onSubmit: @escaping (Bool, String) -> Void) {
        self.maxLength = maxLength
        self.initialText = initialText
        self.onSubmit = onSubmit
        _text = State(initialValue: initialText)
    }

    var body: some View {
        NavigationStack {
            VStack(alignment: .trailing, spacing: 4) {
                TextField("", text: $text)
                    .textFieldStyle(.roundedBorder)
                    .focused($focused)
                    .submitLabel(.done)
                    .onSubmit { submit() }
                    // Enforced as-you-type (not just at submit) -- matches
                    // TextInputActivity.kt's InputFilter.LengthFilter, which
                    // also truncates live rather than only rejecting submit.
                    .onChange(of: text) { _, newValue in
                        if maxLength > 0, newValue.count > Int(maxLength) {
                            text = String(newValue.prefix(Int(maxLength)))
                        }
                    }
                if maxLength > 0 {
                    Text("\(text.count) / \(maxLength)")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
            .padding(20)
            .navigationTitle(LocaleHelper.string("text_input_title", prefs: prefs))
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button(LocaleHelper.string("cancel", prefs: prefs)) {
                        responded = true
                        onSubmit(false, "")
                        dismiss()
                    }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button(LocaleHelper.string("ok", prefs: prefs)) { submit() }
                }
            }
            .onAppear {
                // Not set synchronously -- a fullScreenCover's own
                // presentation animation is still running when this fires,
                // and @FocusState set before that animation completes is a
                // known SwiftUI/iOS quirk that silently never raises the
                // keyboard (the field visibly becomes focused -- cursor
                // blinking -- but the system keyboard just doesn't appear),
                // reported live on a real device where Android's equivalent
                // (TextInputActivity.kt, a real Activity transition, not a
                // cover animation) had no such problem. A short delay past
                // the cover's own transition duration is the standard
                // workaround.
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                    focused = true
                }
            }
            .onDisappear {
                if !responded {
                    onSubmit(false, "")
                }
            }
        }
    }

    private func submit() {
        responded = true
        onSubmit(true, text)
        dismiss()
    }
}

#Preview {
    TextInputView(maxLength: 20, initialText: "Link") { _, _ in }
}
