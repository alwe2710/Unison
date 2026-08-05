import SwiftUI

/// Direct port of AntialiasingActivity.kt: per-console bilinear-vs-nearest
/// toggle, one row per docs/protocol.md stream_type. PlayerViewModel reads
/// whatever's configured here in advance via Prefs.bilinear(for:) once
/// video rendering honors it (currently PlayerView always uses
/// .interpolation(.none) -- wiring this preference through is a later
/// step, not blocking this screen from existing and persisting the right
/// values in the meantime).
private struct ConsoleFilterRow: Identifiable {
    let streamType: String
    let labelKey: String
    var bilinear: Bool
    var id: String { streamType }
}

struct AntialiasingView: View {
    private let prefs = Prefs()
    @State private var rows: [ConsoleFilterRow]

    init() {
        let prefs = Prefs()
        _rows = State(initialValue: [
            ConsoleFilterRow(streamType: GbaStreamClient.streamTypeGcGbaLink, labelKey: "console_gc_gba_link",
                              bilinear: prefs.bilinear(for: GbaStreamClient.streamTypeGcGbaLink)),
            ConsoleFilterRow(streamType: "WIIU_GAMEPAD", labelKey: "console_wiiu_gamepad",
                              bilinear: prefs.bilinear(for: "WIIU_GAMEPAD")),
            ConsoleFilterRow(streamType: "N3DS_BOTTOM_SCREEN", labelKey: "console_n3ds_bottom_screen",
                              bilinear: prefs.bilinear(for: "N3DS_BOTTOM_SCREEN")),
            ConsoleFilterRow(streamType: "NDS_BOTTOM_SCREEN", labelKey: "console_nds_bottom_screen",
                              bilinear: prefs.bilinear(for: "NDS_BOTTOM_SCREEN")),
        ])
    }

    var body: some View {
        List {
            Section {
                Text(LocaleHelper.string("settings_bilinear_filter_header", prefs: prefs))
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
            ForEach($rows) { $row in
                Toggle(LocaleHelper.string(row.labelKey, prefs: prefs), isOn: $row.bilinear)
                    .onChange(of: row.bilinear) { _, newValue in
                        prefs.setBilinear(newValue, for: row.streamType)
                    }
            }
        }
        .navigationTitle(LocaleHelper.string("settings_antialiasing", prefs: prefs))
    }
}

#Preview {
    NavigationStack { AntialiasingView() }
}
