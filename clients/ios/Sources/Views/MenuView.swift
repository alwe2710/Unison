import SwiftUI

/// Carries a connect target (manual entry's parsed port, a discovered
/// single-client server, or a picked GC_GBA_LINK player slot) through
/// SwiftUI's value-based navigation (NavigationLink(value:) +
/// navigationDestination(for:)) -- see MenuView.discoveredRow's own
/// comment for why a dedicated type rather than reusing @State directly.
private struct Connection: Hashable {
    let host: String
    let port: Int32
}

/// Mirrors MenuActivity.kt's connect form fields (manual host:port entry),
/// its UDP discovery-beacon listener (BeaconListener.swift), and its
/// GC_GBA_LINK P1-P4 slot picker (LobbyModel.swift). Tapping Connect
/// (manual entry, single-client host), a discovered single-client server,
/// or a free P-slot pushes PlayerView directly, same "MenuActivity starts
/// PlayerActivity with host/port extras, PlayerActivity does the actual
/// connecting" split as Android.
struct MenuView: View {
    @State private var host: String = ""
    @State private var port: String = "6800"
    @StateObject private var beacon = BeaconListener()
    @StateObject private var lobby = LobbyModel()
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

                        // GC_GBA_LINK's lobby probe -- a bare host (this
                        // app has no combined "host:port" single-field
                        // parsing like MenuActivity.kt's own searchLobby(),
                        // the Port field above already covers that case)
                        // means "search all four player slots", same
                        // GET /status convenience endpoint that file uses.
                        Button(LocaleHelper.string("discovery_start", prefs: prefs)) {
                            search()
                        }
                        .disabled(host.isEmpty || lobby.searching)

                        if lobby.pickerVisible {
                            HStack(spacing: 8) {
                                ForEach(0..<4, id: \.self) { slot in
                                    slotButton(slot)
                                }
                            }
                        }
                        if !lobby.statusText.isEmpty {
                            Text(lobby.statusText)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }

                    Section(LocaleHelper.string("discovery_found_header", prefs: prefs)) {
                        if beacon.servers.isEmpty {
                            Text(LocaleHelper.string("discovery_none_found", prefs: prefs))
                                .foregroundStyle(.secondary)
                        } else {
                            ForEach(beacon.servers) { server in
                                discoveredRow(server)
                            }
                        }
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
            // A plain unconstrained VStack stretches its Form full-width,
            // which reads fine on an iPhone but leaves the text fields
            // absurdly wide on an iPad -- capping and centering the
            // content column is the standard lightweight "adaptive
            // enough" iPad treatment short of a full NavigationSplitView
            // rebuild of this screen (not warranted for a single form).
            .frame(maxWidth: 500)
            .frame(maxWidth: .infinity)
            .navigationDestination(for: Int32.self) { port in
                PlayerView(host: host, port: port)
            }
            .navigationDestination(for: Connection.self) { connection in
                PlayerView(host: connection.host, port: connection.port)
            }
            // Same "settings button opens SettingsActivity" role as
            // MenuActivity.kt's own top-bar icon.
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    NavigationLink {
                        SettingsView()
                    } label: {
                        Image(systemName: "gearshape")
                    }
                    .accessibilityIdentifier("settingsButton")
                }
            }
            .onAppear { beacon.start() }
            .onDisappear { beacon.stop() }
        }
    }

    private func search() {
        let target = host
        Task { await lobby.search(host: target, prefs: prefs) }
    }

    @ViewBuilder
    private func slotButton(_ slot: Int) -> some View {
        let state = lobby.slotStates[slot]
        if state == .free, let searchedHost = lobby.lastSearchedHost {
            NavigationLink(value: Connection(host: searchedHost, port: GbaStreamClient.playerBasePort + Int32(slot))) {
                Text("P\(slot + 1)")
            }
            .buttonStyle(.bordered)
        } else {
            Button("P\(slot + 1)") {}
                .buttonStyle(.bordered)
                .disabled(true)
                .opacity(state == .occupied ? 0.5 : 0.3)
        }
    }

    @ViewBuilder
    private func discoveredRow(_ server: DiscoveredServer) -> some View {
        let title = "\(server.emulatorIdentifier) — \(server.gameTitle)"
        let subtitle = server.compatible
            ? server.streamType
            : server.streamType + LocaleHelper.string("discovery_incompatible_suffix", prefs: prefs)

        if server.compatible, server.streamType != GbaStreamClient.streamTypeGcGbaLink {
            // A dedicated Hashable value type (not the same Int32 the
            // manual-entry Connect button navigates with) -- reusing that
            // one here would mean this row's tap has to write server.host
            // into the shared `host` @State first and hope the
            // navigationDestination(for: Int32.self) closure reads the
            // updated value before building PlayerView, a real ordering
            // hazard between a gesture side effect and SwiftUI's own
            // navigation timing. This carries host+port with the
            // navigation value itself instead, so there's nothing to race.
            NavigationLink(value: Connection(host: server.host, port: server.handshakePort)) {
                discoveredLabel(title: title, subtitle: subtitle)
            }
        } else if server.compatible {
            // GC_GBA_LINK: same "fill the fields and run the P1-P4 probe"
            // treatment as MenuActivity.kt's own discovered-entry handling
            // (runSearch(server.host)) -- its beacon's handshake_port is
            // Dolphin's *lobby* port, not a specific player slot, so there's
            // nothing to connect straight to.
            Button {
                host = server.host
                search()
            } label: {
                discoveredLabel(title: title, subtitle: subtitle)
            }
            .foregroundStyle(.primary)
        } else {
            discoveredLabel(title: title, subtitle: subtitle)
                .foregroundStyle(.secondary)
        }
    }

    @ViewBuilder
    private func discoveredLabel(title: String, subtitle: String) -> some View {
        VStack(alignment: .leading) {
            Text(title)
            Text(subtitle)
                .font(.caption)
                .foregroundStyle(.secondary)
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
