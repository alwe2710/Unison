import SwiftUI

/// Initial scaffold only -- mirrors MenuActivity.kt's connect form fields
/// (manual host:port entry) but not yet its UDP discovery-beacon listener
/// or lobby/slot-picker flow (MenuActivity.kt is ~430 lines; porting all
/// of it, plus PlayerView's actual streaming + the Swift<->unison_core C
/// bridge, is later phases of this client, not this first commit -- see
/// clients/ios/README.md). This exists mainly to give the CI pipeline
/// (XcodeGen + xcodebuild on a macOS runner, see .github/workflows/
/// build.yml's `ios` job) something real to build against.
struct MenuView: View {
    @State private var host: String = ""
    @State private var port: String = "6800"
    private let prefs = Prefs()

    var body: some View {
        NavigationStack {
            VStack(spacing: 16) {
                Text(LocaleHelper.string("app_name", prefs: prefs))
                    .font(.title.bold())

                Form {
                    Section {
                        TextField(LocaleHelper.string("host_hint", prefs: prefs), text: $host)
                            .keyboardType(.URL)
                            .textInputAutocapitalization(.never)
                            .autocorrectionDisabled()
                        TextField("Port", text: $port)
                            .keyboardType(.numberPad)
                    }
                }

                Button(LocaleHelper.string("menu_connect", prefs: prefs)) {
                    // Wired up once GbaStreamClient's Swift<->unison_core
                    // bridge exists (see README.md's phasing).
                }
                .buttonStyle(.borderedProminent)
                .disabled(host.isEmpty)
            }
            .padding()
        }
    }
}

#Preview {
    MenuView()
}
