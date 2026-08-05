import SwiftUI

/// Mirrors MenuActivity.kt's connect form fields (manual host:port entry)
/// but not yet its UDP discovery-beacon listener or lobby/slot-picker flow
/// (MenuActivity.kt is ~430 lines; porting the rest is a later phase, not
/// this one -- see clients/ios/README.md's "Phasing"). Tapping Connect
/// pushes PlayerView, same "MenuActivity starts PlayerActivity with host/
/// port extras, PlayerActivity does the actual connecting" split as
/// Android -- this view's own job stops at picking a host:port.
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

                // NavigationLink(value:) needs a real Int32 (matching
                // navigationDestination(for: Int32.self) below) rather than
                // an Optional -- only offered at all once the port field
                // actually parses to one; wrapped in an always-present but
                // disabled Button otherwise so the control doesn't jump
                // around as the user types.
                if let port = parsedPort {
                    NavigationLink(value: port) {
                        Text(LocaleHelper.string("menu_connect", prefs: prefs))
                    }
                    .buttonStyle(.borderedProminent)
                } else {
                    Button(LocaleHelper.string("menu_connect", prefs: prefs)) {}
                        .buttonStyle(.borderedProminent)
                        .disabled(true)
                }
            }
            .padding()
            .navigationDestination(for: Int32.self) { port in
                PlayerView(host: host, port: port)
            }
        }
    }

    private var parsedPort: Int32? {
        guard let value = Int32(port), value > 0, value <= 65535 else { return nil }
        return value
    }
}

#Preview {
    MenuView()
}
