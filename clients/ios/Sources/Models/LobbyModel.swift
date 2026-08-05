import Foundation

enum SlotState: Equatable {
    case unknown, free, occupied, unreachable
}

/// GC_GBA_LINK's P1-P4 slot picker: plain HTTP GET /status on each of the
/// four player ports (PLAYER_BASE_PORT...+3) -- not through unison_core,
/// same as MenuActivity.kt's own comment on this (GET /status isn't part
/// of the stream protocol, just this one host's own lobby convenience
/// endpoint). Direct port of that file's searchLobby()/fetchOccupied()/
/// applyLobbyResults(), using async/await + URLSession instead of a raw
/// background Thread + HttpURLConnection -- Swift's normal, idiomatic way
/// to do concurrent HTTP requests, no raw-socket work needed here at all
/// (unlike GbaStreamClient/BeaconListener, this isn't part of the wire
/// protocol proper).
@MainActor
final class LobbyModel: ObservableObject {
    @Published private(set) var searching = false
    @Published private(set) var pickerVisible = false
    @Published private(set) var slotStates: [SlotState] = Array(repeating: .unknown, count: 4)
    @Published private(set) var statusText = ""
    private(set) var lastSearchedHost: String?

    func search(host: String, prefs: Prefs) async {
        searching = true
        pickerVisible = false
        statusText = LocaleHelper.string("lobby_searching", prefs: prefs)

        let results = await withTaskGroup(of: (Int, SlotState).self) { group in
            for slot in 0..<4 {
                group.addTask {
                    let port = GbaStreamClient.playerBasePort + Int32(slot)
                    let state = await Self.fetchSlotState(host: host, port: port)
                    return (slot, state)
                }
            }
            var out = Array(repeating: SlotState.unknown, count: 4)
            for await (slot, state) in group {
                out[slot] = state
            }
            return out
        }

        lastSearchedHost = host
        searching = false
        slotStates = results
        // Distinguishes "reached the host fine, every slot is just taken"
        // from "couldn't reach a single one of the four ports" -- both used
        // to show the same "no free slot" message and, worse, the same P1-
        // P4 row regardless, which made a plain single-slot host (Cemu/
        // Azahar/melonDS, or a typo'd address) show four dead buttons
        // exactly as if it *were* a real GC_GBA_LINK/Dolphin multi-slot
        // host. Reported directly after real-device testing: the picker
        // should only appear once a search actually finds an instance that
        // offers multiple players. Matches clients/3ds's own
        // kLobbyNoneConfigured/kLobbyHostUnreachable split (main.cpp's
        // runSearch()).
        let anyFree = results.contains(.free)
        let anyReachable = results.contains { $0 == .free || $0 == .occupied }
        pickerVisible = anyReachable
        if anyFree {
            statusText = LocaleHelper.string("lobby_pick", prefs: prefs)
        } else if anyReachable {
            statusText = LocaleHelper.string("lobby_none_configured", prefs: prefs)
        } else {
            statusText = LocaleHelper.string("lobby_host_unreachable", prefs: prefs)
        }
    }

    /// .unreachable covers both "port not configured as a GBA player slot
    /// at all" and any other network/parse error -- same "null = fine,
    /// don't distinguish the reason" contract as MenuActivity.kt's own
    /// fetchOccupied(). internal (not private) so LobbyModelTests can
    /// exercise the JSON-parsing decision directly against a mocked
    /// URLProtocol, same "extract the pure-enough logic for a real test"
    /// pattern as Prefs.defaultBilinear's own comment.
    static func fetchSlotState(host: String, port: Int32, session: URLSession = .shared) async -> SlotState {
        guard let url = URL(string: "http://\(host):\(port)/status") else { return .unreachable }
        var request = URLRequest(url: url)
        request.timeoutInterval = 1.5
        request.httpMethod = "GET"
        do {
            let (data, _) = try await session.data(for: request)
            let json = try JSONSerialization.jsonObject(with: data) as? [String: Any]
            let occupied = json?["occupied"] as? Bool ?? false
            return occupied ? .occupied : .free
        } catch {
            return .unreachable
        }
    }
}
