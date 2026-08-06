import XCTest
@testable import Unison

/// Direct port of the 3ds/nds clients' own host_port tests
/// (clients/3ds/tests/test_host_port.cpp, clients/nds/arm9/tests/
/// test_host_port.c) -- same gap (MenuView's host field silently dropped
/// an embedded ":port" despite its own host_hint promising exactly that),
/// same fix shape, same test cases.
final class HostPortTests: XCTestCase {
    func testBareHostHasNoPort() {
        XCTAssertNil(splitHostPort("192.168.1.5"))
    }

    func testHostColonPortSplits() {
        let hp = splitHostPort("192.168.1.5:6801")
        XCTAssertEqual(hp, HostPort(host: "192.168.1.5", port: 6801))
    }

    func testTrailingColonWithNoDigitsIsNotAPort() {
        XCTAssertNil(splitHostPort("192.168.1.5:"))
    }

    func testLeadingColonIsNotAPort() {
        XCTAssertNil(splitHostPort(":6801"))
    }

    func testNonNumericSuffixIsNotAPort() {
        XCTAssertNil(splitHostPort("some:thing"))
    }

    func testPortOutOfRangeIsRejected() {
        XCTAssertNil(splitHostPort("192.168.1.5:0"))
        XCTAssertNil(splitHostPort("192.168.1.5:70000"))
        XCTAssertNil(splitHostPort("192.168.1.5:-1"))
    }

    func testTrailingGarbageAfterDigitsIsRejected() {
        XCTAssertNil(splitHostPort("192.168.1.5:6801x"))
    }

    func testHostnameNotJustIpAddressAlsoSplits() {
        // splitHostPort() has no opinion on what a "host" looks like --
        // melonDS's own real-world failure mode this fixes wasn't limited
        // to raw IPs.
        let hp = splitHostPort("melonds.local:6820")
        XCTAssertEqual(hp, HostPort(host: "melonds.local", port: 6820))
    }
}
