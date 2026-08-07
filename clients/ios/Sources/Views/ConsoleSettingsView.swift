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
///
/// Plain console name only, no video-mode subtitle here (an earlier
/// revision showed one, reverted per explicit request) -- the video mode
/// itself is only ever shown/changed one screen further in, on
/// AntialiasingView.
private struct ConsoleRow: Identifiable {
    let streamType: String
    let labelKey: String
    var id: String { streamType }
}

// Alphabetical by displayed label (3DS, GBA/GC, NDS, Wii U), per explicit
// request -- a fixed order rather than a dynamic sort, since all four
// console names are untranslated technical/brand terms, identical across
// every language (see i18n/strings.json), unlike LanguageView's own
// endonym-based sort elsewhere.
private let consoleRows: [ConsoleRow] = [
    ConsoleRow(streamType: "N3DS_BOTTOM_SCREEN", labelKey: "console_n3ds_bottom_screen"),
    ConsoleRow(streamType: GbaStreamClient.streamTypeGcGbaLink, labelKey: "console_gc_gba_link"),
    ConsoleRow(streamType: "NDS_BOTTOM_SCREEN", labelKey: "console_nds_bottom_screen"),
    ConsoleRow(streamType: "WIIU_GAMEPAD", labelKey: "console_wiiu_gamepad"),
]

struct ConsoleSettingsView: View {
    private let prefs = Prefs()

    var body: some View {
        List(consoleRows) { row in
            NavigationLink(LocaleHelper.string(row.labelKey, prefs: prefs)) {
                AntialiasingView(streamType: row.streamType, labelKey: row.labelKey)
            }
        }
        .navigationTitle(LocaleHelper.string("settings_console_specific", prefs: prefs))
    }
}

#Preview {
    NavigationStack { ConsoleSettingsView() }
}
