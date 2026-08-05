import XCTest
@testable import Unison

/// Real exercise of LobbyModel.fetchSlotState()'s JSON-parsing decision --
/// a mocked URLProtocol (URLSession's own standard interception mechanism,
/// not a hand-rolled fake) stands in for a real GET /status response, so
/// this is a genuine request/response round trip through URLSession
/// itself, just without a real socket on the other end.
final class LobbyModelTests: XCTestCase {
    private final class StubURLProtocol: URLProtocol {
        static var responseBody: Data?
        static var shouldFail = false

        override class func canInit(with request: URLRequest) -> Bool { true }
        override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

        override func startLoading() {
            if Self.shouldFail {
                client?.urlProtocol(self, didFailWithError: URLError(.cannotConnectToHost))
                return
            }
            let response = HTTPURLResponse(url: request.url!, statusCode: 200, httpVersion: nil, headerFields: nil)!
            client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
            client?.urlProtocol(self, didLoad: Self.responseBody ?? Data())
            client?.urlProtocolDidFinishLoading(self)
        }

        override func stopLoading() {}
    }

    private var session: URLSession!

    override func setUp() {
        super.setUp()
        let config = URLSessionConfiguration.ephemeral
        config.protocolClasses = [StubURLProtocol.self]
        session = URLSession(configuration: config)
        StubURLProtocol.shouldFail = false
        StubURLProtocol.responseBody = nil
    }

    func testOccupiedTrueMapsToOccupied() async {
        StubURLProtocol.responseBody = #"{"occupied": true}"#.data(using: .utf8)
        let state = await LobbyModel.fetchSlotState(host: "192.168.1.1", port: 6801, session: session)
        XCTAssertEqual(state, .occupied)
    }

    func testOccupiedFalseMapsToFree() async {
        StubURLProtocol.responseBody = #"{"occupied": false}"#.data(using: .utf8)
        let state = await LobbyModel.fetchSlotState(host: "192.168.1.1", port: 6801, session: session)
        XCTAssertEqual(state, .free)
    }

    func testMissingOccupiedFieldDefaultsToFree() async {
        StubURLProtocol.responseBody = #"{}"#.data(using: .utf8)
        let state = await LobbyModel.fetchSlotState(host: "192.168.1.1", port: 6801, session: session)
        XCTAssertEqual(state, .free)
    }

    func testMalformedJSONMapsToUnreachable() async {
        StubURLProtocol.responseBody = "not json".data(using: .utf8)
        let state = await LobbyModel.fetchSlotState(host: "192.168.1.1", port: 6801, session: session)
        XCTAssertEqual(state, .unreachable)
    }

    func testNetworkFailureMapsToUnreachable() async {
        StubURLProtocol.shouldFail = true
        let state = await LobbyModel.fetchSlotState(host: "192.168.1.1", port: 6801, session: session)
        XCTAssertEqual(state, .unreachable)
    }
}
