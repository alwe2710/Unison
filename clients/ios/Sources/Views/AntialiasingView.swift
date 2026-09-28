import SwiftUI

/// Per-console detail screen, reached from ConsoleSettingsView's 4-console
/// list -- used to be that list itself (one row per docs/protocol.md
/// stream_type, each an inline bilinear Switch and nothing else); moved out
/// to ConsoleSettingsView so this screen could show more than a single
/// toggle per console without cramming both the antialiasing switch and a
/// video-mode picker into one flat list row. Two settings live here, both
/// keyed by this one streamType: bilinear-vs-nearest upscale (unchanged
/// from before -- PlayerViewModel reads whatever's configured here via
/// Prefs.bilinear(for:), see PlayerView's own .interpolation() comment) and
/// the video-mode/compression picker (own sub-screen, VideoModeView) --
/// used to be a single global choice shared by every console (SettingsView's
/// own former "Videomodus" row), moved here per console for the same reason
/// antialiasing already was.
struct AntialiasingView: View {
    let streamType: String
    let labelKey: String

    private let prefs = Prefs()
    @State private var bilinear: Bool
    @State private var videoMode: String
    @State private var secondStickEnabled: Bool

    init(streamType: String, labelKey: String) {
        self.streamType = streamType
        self.labelKey = labelKey
        let prefs = Prefs()
        _bilinear = State(initialValue: prefs.bilinear(for: streamType))
        _videoMode = State(initialValue: prefs.videoMode(for: streamType))
        _secondStickEnabled = State(initialValue: prefs.n3dsSecondStickEnabled)
    }

    var body: some View {
        Form {
            Section {
                Toggle(LocaleHelper.string("settings_antialiasing", prefs: prefs), isOn: $bilinear)
                    .onChange(of: bilinear) { _, newValue in
                        prefs.setBilinear(newValue, for: streamType)
                    }

                // N3DS_BOTTOM_SCREEN only -- WIIU_GAMEPAD keeps its own
                // always-on second stick (real Wii U GamePad hardware), no
                // toggle needed there; no other console has a stick at all.
                // See Prefs.n3dsSecondStickEnabled's own comment on why
                // this exists (previously always shown for any hasSticks
                // session).
                if streamType == "N3DS_BOTTOM_SCREEN" {
                    Toggle(LocaleHelper.string("settings_second_stick", prefs: prefs), isOn: $secondStickEnabled)
                        .onChange(of: secondStickEnabled) { _, newValue in
                            prefs.n3dsSecondStickEnabled = newValue
                        }
                    Text(LocaleHelper.string("settings_second_stick_hint", prefs: prefs))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                NavigationLink {
                    VideoModeView(streamType: streamType)
                } label: {
                    VStack(alignment: .leading) {
                        Text(LocaleHelper.string("settings_video_mode", prefs: prefs))
                        Text(LocaleHelper.string(
                            Prefs.videoModes.first { $0.value == videoMode }?.labelKey ?? "video_mode_tiles",
                            prefs: prefs))
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            }
        }
        .navigationTitle(LocaleHelper.string(labelKey, prefs: prefs))
        // Picking a video mode in VideoModeView and returning here wouldn't
        // otherwise refresh this screen's own subtitle -- same
        // language-row staleness fix SettingsView.onAppear already needed.
        .onAppear {
            bilinear = prefs.bilinear(for: streamType)
            videoMode = prefs.videoMode(for: streamType)
            secondStickEnabled = prefs.n3dsSecondStickEnabled
        }
    }
}

#Preview {
    NavigationStack { AntialiasingView(streamType: "WIIU_GAMEPAD", labelKey: "console_wiiu_gamepad") }
}
