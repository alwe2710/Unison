import SwiftUI

/// Direct port of VideoModeActivity.kt: same "own sub-screen, tap a row,
/// pick and return" shape as LanguageView, but deliberately NOT sorted by
/// label -- these aren't endonyms, and Prefs.videoModes' declared order
/// (recommended default first) is intentional.
struct VideoModeView: View {
    private let prefs = Prefs()
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        List(Prefs.videoModes, id: \.value) { option in
            Button(LocaleHelper.string(option.labelKey, prefs: prefs)) {
                prefs.videoMode = option.value
                dismiss()
            }
            .foregroundStyle(.primary)
        }
        .navigationTitle(LocaleHelper.string("settings_video_mode", prefs: prefs))
    }
}

#Preview {
    NavigationStack { VideoModeView() }
}
