import SwiftUI

/// Direct port of the other clients' Info screens: app name, dynamic
/// version (UnisonVersion.gitDescribe, written at build time by
/// project.yml's preBuildScripts phase from `git describe` -- see
/// Sources/Generated/Version.swift's own header comment), license, repo
/// link. Unlike the 3DS/Switch Info screens, the repo link here is a real
/// tappable Link (opens Safari) -- iOS can open arbitrary URLs, those two
/// homebrew targets can't.
///
/// MARKETING_VERSION (the AltStore-tracked App Store version, see
/// project.yml's own comment on docs/altstore/apps.json) is shown too, but
/// kept separate from gitDescribe -- this screen must never make AltStore's
/// byte-for-byte version-matching requirement ambiguous by conflating the
/// two into one string.
struct InfoView: View {
    private let prefs = Prefs()

    private var marketingVersion: String {
        Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "?"
    }

    var body: some View {
        List {
            Section {
                Text(LocaleHelper.string("app_name", prefs: prefs))
                    .font(.title2)
                Text(String(format: LocaleHelper.string("info_version", prefs: prefs), UnisonVersion.gitDescribe))
                Text(String(format: LocaleHelper.string("info_license", prefs: prefs), "PolyForm Noncommercial 1.0.0"))
            }

            Section {
                Link(destination: URL(string: "https://github.com/alwe2710/Unison")!) {
                    Text(LocaleHelper.string("info_repo", prefs: prefs))
                }
            }

            Section {
                Text("App Store: \(marketingVersion)")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .navigationTitle(LocaleHelper.string("settings_info", prefs: prefs))
    }
}

#Preview {
    NavigationStack { InfoView() }
}
