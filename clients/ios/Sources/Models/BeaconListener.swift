import Darwin
import Foundation

/// One server seen via the UDP discovery beacon (docs/protocol.md,
/// "Discovery beacon (UDP)"). Direct port of MenuActivity.kt's private
/// DiscoveredServer data class -- `compatible` mirrors the beacon's
/// protocol_version against UNISON_PROTOCOL_VERSION the same way.
struct DiscoveredServer: Identifiable, Equatable {
    let host: String
    let emulatorIdentifier: String
    let gameTitle: String
    let streamType: String
    let handshakePort: Int32
    let protocolVersion: Int32
    var lastSeen: Date

    var compatible: Bool { protocolVersion == Int32(UNISON_PROTOCOL_VERSION) }
    var id: String { "\(host):\(handshakePort):\(streamType)" }

    static func == (lhs: DiscoveredServer, rhs: DiscoveredServer) -> Bool { lhs.id == rhs.id }

    init(from beacon: unison_beacon) {
        protocolVersion = Int32(beacon.protocol_version)
        emulatorIdentifier = Self.string(from: beacon.emulator_identifier)
        gameTitle = Self.string(from: beacon.game_title)
        streamType = Self.string(from: beacon.stream_type)
        host = Self.string(from: beacon.host)
        handshakePort = Int32(beacon.handshake_port)
        lastSeen = Date()
    }

    /// unison_beacon's char[] fields import into Swift as fixed-size
    /// tuples of CChar, not String -- the standard way to read one back
    /// out is to reinterpret its raw bytes as a NUL-terminated C string.
    private static func string<T>(from tuple: T) -> String {
        withUnsafeBytes(of: tuple) { raw in
            String(cString: raw.baseAddress!.assumingMemoryBound(to: CChar.self))
        }
    }
}

/// Listens for the UDP beacon every Unison server broadcasts (real POSIX
/// UDP socket, background thread) and parses it via unison_core's own
/// unison_parse_beacon() (core/src/discovery.c, pure parsing -- this class
/// only owns the socket I/O, same core/client split as GbaStreamClient's
/// own unison_native_bridge.c). Direct port of MenuActivity.kt's own
/// discovery listener, minus the lobby/slot-picker probing it also does
/// for GC_GBA_LINK entries specifically -- see MenuView's own comment for
/// why that's a separate, later addition (this class's job stops at
/// reporting what's out there).
final class BeaconListener: ObservableObject {
    @Published private(set) var servers: [DiscoveredServer] = []

    private var socketFD: Int32 = -1
    private var staleTimer: Timer?

    func start() {
        stop()

        let fd = socket(AF_INET, SOCK_DGRAM, 0)
        guard fd >= 0 else { return }
        var reuse: Int32 = 1
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, socklen_t(MemoryLayout<Int32>.size))

        var addr = sockaddr_in()
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_addr.s_addr = INADDR_ANY
        addr.sin_port = UInt16(UNISON_BEACON_PORT).bigEndian
        let bindResult = withUnsafePointer(to: &addr) { ptr in
            ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sockaddrPtr in
                bind(fd, sockaddrPtr, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        guard bindResult == 0 else {
            close(fd)
            return
        }
        socketFD = fd

        let thread = Thread { [weak self] in
            self?.receiveLoop(fd: fd)
        }
        thread.name = "unison-beacon-listener"
        thread.start()

        // Entries older than UNISON_BEACON_STALE_MS are considered gone,
        // same client-side convention as docs/protocol.md documents (a
        // server that stopped broadcasting -- closed, network dropped --
        // should disappear from the list, not linger forever).
        staleTimer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            self?.pruneStale()
        }
    }

    /// Closing the socket is what makes receiveLoop's blocking recv() on
    /// the background thread return (with an error) and exit on its own --
    /// no separate stop-flag/thread-join needed for a single recv() call
    /// the way unison_native_bridge.c's session loop (a real ongoing
    /// read/write loop) needs one.
    func stop() {
        if socketFD >= 0 {
            close(socketFD)
            socketFD = -1
        }
        staleTimer?.invalidate()
        staleTimer = nil
    }

    private func receiveLoop(fd: Int32) {
        var buffer = [UInt8](repeating: 0, count: 2048)
        while true {
            let n = recv(fd, &buffer, buffer.count, 0)
            if n <= 0 {
                return // socket closed (stop()) or a real error either way
            }
            var beacon = unison_beacon()
            let ok = buffer.withUnsafeMutableBufferPointer { ptr in
                unison_parse_beacon(ptr.baseAddress, n, &beacon) != 0
            }
            guard ok else { continue } // unrelated UDP noise on the same port, see unison_parse_beacon's own comment

            let server = DiscoveredServer(from: beacon)
            DispatchQueue.main.async { [weak self] in
                self?.upsert(server)
            }
        }
    }

    private func upsert(_ server: DiscoveredServer) {
        if let index = servers.firstIndex(where: { $0.id == server.id }) {
            servers[index] = server
        } else {
            servers.append(server)
        }
    }

    private func pruneStale() {
        let cutoff = Date().addingTimeInterval(-Double(UNISON_BEACON_STALE_MS) / 1000)
        servers.removeAll { $0.lastSeen < cutoff }
    }
}
