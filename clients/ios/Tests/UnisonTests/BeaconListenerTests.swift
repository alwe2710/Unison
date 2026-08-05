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

        // Retries the send rather than a single fixed sleep-then-send:
        // there's no synchronous confirmation that the listener's
        // background thread has finished bind()-ing before this test
        // sends its first packet (a UDP send to a not-yet-bound local
        // port doesn't itself fail), so resending periodically until the
        // listener actually reports something is the robust way to avoid
        // a race against that startup, not a magic-number sleep duration.
        let deadline = Date().addingTimeInterval(5)
        while listener.servers.isEmpty && Date() < deadline {
            Self.sendUDP(json, toPort: UInt16(UNISON_BEACON_PORT))
            RunLoop.current.run(until: Date().addingTimeInterval(0.2))
        }

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
