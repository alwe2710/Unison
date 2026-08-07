import SwiftUI

/// Direct port of VideoModeActivity.kt: same "own sub-screen, tap a row,
/// pick and return" shape as LanguageView, but deliberately NOT sorted by
/// label -- these aren't endonyms, and Prefs.videoModes' declared order
/// (recommended default first) is intentional.
///
/// Per-console (streamType, always launched from AntialiasingView now)
/// rather than one global choice -- see Prefs.videoMode(for:)'s own comment
/// on why (picking H.264 for Cemu used to silently also request it from
/// Dolphin next time, which never honors it anyway, but still).
struct VideoModeView: View {
    let streamType: String

    private let prefs = Prefs()
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        List(Prefs.videoModes, id: \.value) { option in
            Button(LocaleHelper.string(option.labelKey, prefs: prefs)) {
                prefs.setVideoMode(option.value, for: streamType)
                dismiss()
            }
            .foregroundStyle(.primary)
        }
        .navigationTitle(LocaleHelper.string("settings_video_mode", prefs: prefs))
    }
}

#Preview {
    NavigationStack { VideoModeView(streamType: "WIIU_GAMEPAD") }
}
