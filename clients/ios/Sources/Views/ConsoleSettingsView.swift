import SwiftUI

/// Top of the "Konsolenspezifische Einstellungen" nav row (SettingsView) --
/// lists the four consoles Unison streams from, each a NavigationLink into
/// AntialiasingView (now the per-console detail screen: a bilinear-filter
/// switch plus a video-mode picker, both keyed by that console's own
/// stream_type). Replaces what used to be two separate top-level
/// SettingsView rows (a global "Bilineare Filterung" screen that WAS this
/// list, just with an inline Toggle per row instead of a nav target; and a
/// global "Videomodus" row/screen shared by every console) -- see
/// AntialiasingView's/VideoModeView's own comments on why each setting
/// moved from "one value for every console" to "one value per console".
private struct ConsoleRow: Identifiable {
    let streamType: String
    let labelKey: String
    var videoModeLabelKey: String
    var id: String { streamType }
}

struct ConsoleSettingsView: View {
    private let prefs = Prefs()
    @State private var rows: [ConsoleRow]

    init() {
        _rows = State(initialValue: Self.loadRows())
    }

    var body: some View {
        List(rows) { row in
            NavigationLink {
                AntialiasingView(streamType: row.streamType, labelKey: row.labelKey)
            } label: {
                VStack(alignment: .leading) {
                    Text(LocaleHelper.string(row.labelKey, prefs: prefs))
                    Text(LocaleHelper.string(row.videoModeLabelKey, prefs: prefs))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
        }
        .navigationTitle(LocaleHelper.string("settings_console_specific", prefs: prefs))
        // Refreshed on return from AntialiasingView too (picking a new video
        // mode there and coming back here shouldn't leave this list's own
        // subtitles stale), same reasoning as AntialiasingView's own
        // onAppear re-read.
        .onAppear {
            rows = Self.loadRows()
        }
    }

    private static func loadRows() -> [ConsoleRow] {
        let prefs = Prefs()
        let consoles: [(String, String)] = [
            (GbaStreamClient.streamTypeGcGbaLink, "console_gc_gba_link"),
            ("WIIU_GAMEPAD", "console_wiiu_gamepad"),
            ("N3DS_BOTTOM_SCREEN", "console_n3ds_bottom_screen"),
            ("NDS_BOTTOM_SCREEN", "console_nds_bottom_screen"),
        ]
        return consoles.map { streamType, labelKey in
            let videoMode = prefs.videoMode(for: streamType)
            let videoModeLabelKey = Prefs.videoModes.first { $0.value == videoMode }?.labelKey ?? "video_mode_tiles"
            return ConsoleRow(streamType: streamType, labelKey: labelKey, videoModeLabelKey: videoModeLabelKey)
        }
    }
}

#Preview {
    NavigationStack { ConsoleSettingsView() }
}
