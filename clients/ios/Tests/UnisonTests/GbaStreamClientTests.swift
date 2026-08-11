import Darwin
import XCTest
@testable import Unison

/// Real end-to-end exercise of the Swift<->unison_native_bridge.c<->
/// unison_core plumbing -- proves the background pthread actually spawns,
/// makes a real POSIX socket connect() attempt, and marshals the result
/// back across the C callback boundary into Swift (Unmanaged retain/
/// release, @convention(c) closures) without crashing or leaking, before
/// any UI is built on top of it. Deliberately targets a real closed port
/// rather than a fake server (a full protocol round-trip test -- real
/// hello/hello_ack/session_ready + a decoded video frame -- needs a fake
/// WS server fixture unison_core has no server-side framing helpers for,
/// see websocket.h's own "Client-side" comment; that's a later addition
/// once PlayerView needs to exercise it, not blocking this MVP).
final class GbaStreamClientTests: XCTestCase {
    private final class RecordingListener: GbaStreamClient.Listener {
        let disconnectedExpectation: XCTestExpectation
        var disconnectReason: String?

        init(disconnectedExpectation: XCTestExpectation) {
            self.disconnectedExpectation = disconnectedExpectation
        }

        func onConnected(touchInput: Bool, hasButtons: Bool, hasSticks: Bool, width: Int32, height: Int32,
                          grantedVideoMode: String, streamType: String) {
            XCTFail("a closed port should never report onConnected")
        }

        func onVideoFrame(width: Int32, height: Int32, rgb565: Data) {
            XCTFail("a closed port should never report onVideoFrame")
        }

        func onCompressedVideoFrame(width: Int32, height: Int32, isH265: Bool, data: UnsafeRawBufferPointer) {
            XCTFail("a closed port should never report onCompressedVideoFrame")
        }

        func onAudioFrame(sampleRate: Int32, channels: Int32, pcm: [Int16]) {
            XCTFail("a closed port should never report onAudioFrame")
        }

        func onTextInputRequest(maxLength: Int32, initialText: String) {
            XCTFail("a closed port should never report onTextInputRequest")
        }

        func onDisconnected(reason: String) {
            disconnectReason = reason
            disconnectedExpectation.fulfill()
        }
    }

    func testConnectToClosedPortReportsDisconnected() {
        // A definitely-closed port: bind a socket to 127.0.0.1:0 (OS picks
        // a free ephemeral port), then close it immediately -- nothing is
        // listening there by construction, so the real background
        // thread's connect() is guaranteed to fail (ECONNREFUSED), same
        // as a real "server not running" case a user would actually hit.
        let port = Self.closedPort()

        let expectation = expectation(description: "onDisconnected fires")
        let listener = RecordingListener(disconnectedExpectation: expectation)
        let client = GbaStreamClient(listener: listener)

        client.connect(host: "127.0.0.1", port: port)
        wait(for: [expectation], timeout: 5)

        XCTAssertEqual(listener.disconnectReason, "Verbindung fehlgeschlagen")

        // Must not crash/hang/double-free even though the background
        // thread has already finished and called back on its own --
        // real proof unison_native_disconnect()'s pthread_join is safe
        // to call on an already-terminated thread.
        client.disconnect()
    }

    private static func closedPort() -> Int32 {
        let fd = socket(AF_INET, SOCK_STREAM, 0)
        defer { close(fd) }
        var addr = sockaddr_in()
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_addr.s_addr = inet_addr("127.0.0.1")
        addr.sin_port = 0
        let bindResult = withUnsafePointer(to: &addr) { ptr in
            ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sockaddrPtr in
                bind(fd, sockaddrPtr, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        XCTAssertEqual(bindResult, 0)

        var actual = sockaddr_in()
        var len = socklen_t(MemoryLayout<sockaddr_in>.size)
        _ = withUnsafeMutablePointer(to: &actual) { ptr in
            ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sockaddrPtr in
                getsockname(fd, sockaddrPtr, &len)
            }
        }
        return Int32(UInt16(bigEndian: actual.sin_port))
    }
}
