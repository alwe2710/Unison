import SwiftUI

/// Direct port of LanguageActivity.kt: plain rows, no radio buttons/
/// checkmarks -- tapping a row sets the preference and immediately
/// returns to SettingsView (dismiss()), same one-shot "pick and you're
/// done" flow as the Android original.
struct LanguageView: View {
    private let prefs = Prefs()
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        // Sorted by the displayed label, not a fixed order -- "System" is
        // localized like any other UI string, but "Deutsch"/"English" are
        // fixed endonyms (see i18n/strings.json), same as
        // LanguageActivity.kt's own sortedBy { it.second }.
        let options = Prefs.languages
            .map { (value: $0.value, label: LocaleHelper.string($0.labelKey, prefs: prefs)) }
            .sorted { $0.label < $1.label }

        List(options, id: \.value) { option in
            Button(option.label) {
                prefs.language = option.value
                dismiss()
            }
            .foregroundStyle(.primary)
        }
        .navigationTitle(LocaleHelper.string("settings_language", prefs: prefs))
    }
}

#Preview {
    NavigationStack { LanguageView() }
}
