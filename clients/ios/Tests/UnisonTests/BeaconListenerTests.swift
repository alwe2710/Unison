import Combine
import Darwin
import XCTest
@testable import Unison

/// Real end-to-end exercise of BeaconListener: a real UDP socket bound to
/// UNISON_BEACON_PORT, a real hand-crafted beacon packet (exact wire
/// format from docs/protocol.md's own "Discovery beacon (UDP)" example)
/// sent to 127.0.0.1 from this test, and a real unison_parse_beacon() call
/// on the receiving end -- same "prove the real socket/callback plumbing
/// works, not just that it compiles" bar as GbaStreamClientTests.swift.
final class BeaconListenerTests: XCTestCase {
    func testReceivesAndParsesARealBeaconPacket() {
        let listener = BeaconListener()
        XCTAssertTrue(listener.start(), "BeaconListener.start() itself failed -- see NSLog output for the errno")
        defer { listener.stop() }

        let json = """
        {"type":"unison_beacon","protocol_version":2,"emulator_identifier":"TestEmu",\
        "game_title":"Test Game","stream_type":"N3DS_BOTTOM_SCREEN","host":"127.0.0.1",\
        "handshake_port":6810}
        """

        // Real first CI run (2026-08-05): NSLog diagnostics showed recv()
        // and unison_parse_beacon() succeeding repeatedly, but the plain
        // `while listener.servers.isEmpty { RunLoop.current.run(until:) }`
        // polling loop this replaced never observed it -- @Published's
        // main-queue dispatch from the background recv thread apparently
        // isn't reliably drained by a bare nested RunLoop.run(until:) call
        // inside a running XCTest method. XCTestExpectation/XCTWaiter (via
        // a Combine subscription to $servers) is XCTest's own blessed
        // mechanism for waiting on exactly this kind of cross-thread async
        // update and is what should actually be trusted here instead.
        let expectation = expectation(description: "beacon received")
        var cancellable: AnyCancellable?
        cancellable = listener.$servers
            .filter { !$0.isEmpty }
            .first()
            .sink { _ in expectation.fulfill() }

        // Still resends periodically rather than once -- the listener's
        // background thread may not have finished bind()-ing by the time
        // this first send goes out, and a UDP send to a not-yet-bound
        // local port doesn't itself fail or block.
        let resendTimer = Timer.scheduledTimer(withTimeInterval: 0.2, repeats: true) { _ in
            Self.sendUDP(json, toPort: UInt16(UNISON_BEACON_PORT))
        }
        resendTimer.fire()
        defer {
            resendTimer.invalidate()
            cancellable?.cancel()
        }

        wait(for: [expectation], timeout: 5)

        guard let server = listener.servers.first else {
            XCTFail("BeaconListener never reported the test packet")
            return
        }
        XCTAssertEqual(server.emulatorIdentifier, "TestEmu")
        XCTAssertEqual(server.gameTitle, "Test Game")
        XCTAssertEqual(server.streamType, "N3DS_BOTTOM_SCREEN")
        XCTAssertEqual(server.host, "127.0.0.1")
        XCTAssertEqual(server.handshakePort, 6810)
        XCTAssertTrue(server.compatible)
    }

    private static func sendUDP(_ text: String, toPort port: UInt16) {
        let fd = socket(AF_INET, SOCK_DGRAM, 0)
        guard fd >= 0 else {
            NSLog("BeaconListenerTests: send socket() failed, errno=\(errno)")
            return
        }
        defer { close(fd) }

        var addr = sockaddr_in()
        addr.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port = port.bigEndian
        addr.sin_addr.s_addr = inet_addr("127.0.0.1")

        let bytes = Array(text.utf8)
        let result = withUnsafePointer(to: &addr) { ptr in
            ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sockaddrPtr in
                bytes.withUnsafeBufferPointer { buf in
                    sendto(fd, buf.baseAddress, buf.count, 0, sockaddrPtr, socklen_t(MemoryLayout<sockaddr_in>.size))
                }
            }
        }
        if result < 0 {
            NSLog("BeaconListenerTests: sendto() failed, errno=\(errno) (\(String(cString: strerror(errno))))")
        } else {
            NSLog("BeaconListenerTests: sendto() sent \(result) bytes to port \(port)")
        }
    }
}
