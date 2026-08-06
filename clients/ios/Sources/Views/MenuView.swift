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
    // Forwarded to every PlayerView this screen ever pushes -- see that
    // property's own comment (PlayerView.swift). Not consulted here at
    // all, just threaded through.
    var onPlayerActiveChanged: ((Bool) -> Void)? = nil

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

                        if !lobby.statusText.isEmpty {
                            Text(lobby.statusText)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }

                    // Reported: "no way to manually trigger the beacon
                    // scan" -- the "Search for servers" button above isn't
                    // that (it needs a host already typed above, and probes
                    // *that* host's four player slots specifically; the
                    // passive UDP beacon listener this section shows is a
                    // different mechanism entirely, running continuously in
                    // the background with nothing to "trigger"). This
                    // restarts that listener on demand -- a real, visible
                    // way to retry discovery without relaunching the app,
                    // useful in particular if the iOS Local Network
                    // permission prompt was just granted (Settings ->
                    // Privacy & Security -> Local Network -> Unison) after
                    // initially being denied/dismissed, one of this
                    // client's leading suspects for "beacon search doesn't
                    // find anything" reports.
                    Section {
                        if beacon.servers.isEmpty {
                            Text(LocaleHelper.string("discovery_none_found", prefs: prefs))
                                .foregroundStyle(.secondary)
                        } else {
                            ForEach(beacon.servers) { server in
                                discoveredRow(server)
                            }
                        }
                    } header: {
                        HStack {
                            Text(LocaleHelper.string("discovery_found_header", prefs: prefs))
                            Spacer()
                            Button {
                                beacon.stop()
                                beacon.start()
                            } label: {
                                Image(systemName: "arrow.clockwise")
                            }
                            .accessibilityLabel(Text(LocaleHelper.string("discovery_start", prefs: prefs)))
                        }
                    }
                }

                // Deliberately NOT a Form/List row (moved out of the Form's
                // Section above, where this used to live as four
                // NavigationLinks crammed into one HStack): reported
                // directly after real-device testing as navigating to the
                // wrong slot (tapping P1 opened P2's PlayerView, and Back
                // popped through a *second* stacked PlayerView before
                // reaching the menu) -- Form/List rows are built around one
                // primary tap target per row, and four separate
                // NavigationLinks sharing one row's tap-gesture/selection
                // machinery is exactly the kind of setup known to misfire
                // like that. A plain HStack outside the Form, same pattern
                // the Connect button below already uses, sidesteps List row
                // semantics entirely instead of trying to out-guess them.
                if lobby.pickerVisible {
                    HStack(spacing: 8) {
                        ForEach(0..<4, id: \.self) { slot in
                            slotButton(slot)
                        }
                    }
                }

                // NavigationLink(value:) needs a real Int32 (matching
                // navigationDestination(for: Int32.self) below) rather than
                // an Optional -- only offered at all once the port field
                // actually parses to one; wrapped in an always-present but
                // disabled Button otherwise so the control doesn't jump
                // around as the user types.
                //
                // .borderless (plain tinted text, no filled pill) rather
                // than .borderedProminent -- reported directly after
                // real-device testing as not matching the standard iOS
                // convention (system apps use colored text for this kind of
                // action, not a filled white-on-color button). .font(.body.bold())
                // keeps it visually the screen's primary action despite the
                // lighter-weight style.
                if let port = parsedPort {
                    NavigationLink(value: port) {
                        Text(LocaleHelper.string("menu_connect", prefs: prefs))
                            .font(.body.bold())
                    }
                    .buttonStyle(.borderless)
                } else {
                    Button(LocaleHelper.string("menu_connect", prefs: prefs)) {}
                        .font(.body.bold())
                        .buttonStyle(.borderless)
                        .disabled(true)
                }
            }
            .padding()
            // No maxWidth cap here anymore (there used to be one, 500pt
            // centered) -- reported as a visible "gray frame" on iPad's
            // wide landscape layout: capping this screen's own content
            // left a plain-background margin on both sides of the capped
            // Form, and Form's own inset-grouped background (a distinct
            // gray) meeting that plain margin read as an unwanted border/
            // frame right at the cap's edges, worse the wider the
            // surrounding space was. That cap predates RootView's
            // NavigationSplitView, back when this screen was shown
            // full-screen-wide with no sidebar constraining it at all --
            // the sidebar's own detail pane already keeps this screen from
            // ever being absurdly, phone-form-on-a-cinema-screen wide, so
            // the second layer of capping is redundant now, not just
            // visually broken.
            .navigationDestination(for: Int32.self) { port in
                PlayerView(host: host, port: port, onActiveChanged: onPlayerActiveChanged)
            }
            .navigationDestination(for: Connection.self) { connection in
                PlayerView(host: connection.host, port: connection.port, onActiveChanged: onPlayerActiveChanged)
            }
            // No gear-icon toolbar link to SettingsView anymore -- Settings
            // is its own top-level sidebar entry now (RootView.swift), same
            // level as Connect rather than nested a screen below it.
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
