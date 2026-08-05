import SwiftUI

/// Direct port of SettingsActivity.kt: on-screen-controls toggle, plus
/// navigation rows into VideoModeView/LanguageView/AntialiasingView/
/// KeyBindingsView (their own screens, same split as Android -- see each
/// Kotlin file's own comment for why). This screen doesn't touch key
/// bindings or per-console filter state directly at all, same as the
/// original.
struct SettingsView: View {
    private let prefs = Prefs()

    @State private var onScreenControlsEnabled: Bool
    @State private var language: String
    @State private var videoMode: String

    init() {
        let prefs = Prefs()
        _onScreenControlsEnabled = State(initialValue: prefs.onScreenControlsEnabled)
        _language = State(initialValue: prefs.language)
        _videoMode = State(initialValue: prefs.videoMode)
    }

    var body: some View {
        Form {
            Section {
                Toggle(LocaleHelper.string("settings_on_screen_controls", prefs: prefs),
                       isOn: $onScreenControlsEnabled)
                    .accessibilityIdentifier("onScreenControlsToggle")
                    .onChange(of: onScreenControlsEnabled) { _, newValue in
                        prefs.onScreenControlsEnabled = newValue
                    }
            }

            Section {
                NavigationLink {
                    VideoModeView()
                } label: {
                    settingsRow(
                        title: "settings_video_mode",
                        subtitle: Prefs.videoModes.first { $0.value == videoMode }?.labelKey ?? "video_mode_tiles")
                }

                NavigationLink {
                    LanguageView()
                } label: {
                    settingsRow(
                        title: "settings_language",
                        subtitle: Prefs.languages.first { $0.value == language }?.labelKey ?? "language_system")
                }

                NavigationLink(LocaleHelper.string("settings_antialiasing", prefs: prefs)) {
                    AntialiasingView()
                }

                NavigationLink(LocaleHelper.string("settings_key_bindings", prefs: prefs)) {
                    KeyBindingsView()
                }
            }
        }
        .navigationTitle(LocaleHelper.string("settings", prefs: prefs))
        // SettingsActivity.kt's onResume() re-reads Prefs.language/videoMode
        // directly (see its own comment on why the locale-mismatch
        // recreate() alone doesn't always catch a "System" pick that
        // resolves to the same language as before) -- onAppear is this
        // view's closest equivalent, firing every time this screen becomes
        // visible again (returning from LanguageView/VideoModeView included).
        .onAppear {
            language = prefs.language
            videoMode = prefs.videoMode
        }
    }

    @ViewBuilder
    private func settingsRow(title: String, subtitle: String) -> some View {
        VStack(alignment: .leading) {
            Text(LocaleHelper.string(title, prefs: prefs))
            Text(LocaleHelper.string(subtitle, prefs: prefs))
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }
}

#Preview {
    NavigationStack { SettingsView() }
}
