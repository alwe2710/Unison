import XCTest
@testable import Unison

/// Real, deterministic check of PlayerViewModel.image(fromRGB565:width:height:)
/// -- the one piece of PlayerView's pipeline that's pure enough to unit
/// test without a Simulator-rendered UI or a real network session (see
/// GbaStreamClientTests.swift for that side of the pipeline).
final class PlayerViewModelTests: XCTestCase {
    func testDecodesKnownRGB565Pixels() {
        // Pure red (0xF800), pure green (0x07E0), pure black (0x0000),
        // pure white (0xFFFF) -- u16le, so low byte first.
        let bytes: [UInt8] = [
            0x00, 0xF8, // red
            0xE0, 0x07, // green
            0x00, 0x00, // black
            0xFF, 0xFF, // white
        ]
        let data = Data(bytes)

        let image = PlayerViewModel.image(fromRGB565: data, width: 2, height: 2)
        XCTAssertNotNil(image)

        guard let cgImage = image?.cgImage, let provider = cgImage.dataProvider,
              let pixelData = provider.data
        else {
            XCTFail("expected a real CGImage with pixel data")
            return
        }
        let ptr = CFDataGetBytePtr(pixelData)!

        // 5-bit/6-bit -> 8-bit expansion: 0x1F -> 255, 0x3F -> 255, 0 -> 0.
        XCTAssertEqual(ptr[0], 255) // red.r
        XCTAssertEqual(ptr[1], 0) // red.g
        XCTAssertEqual(ptr[2], 0) // red.b
        XCTAssertEqual(ptr[3], 255) // alpha (always opaque)

        XCTAssertEqual(ptr[4], 0) // green.r
        XCTAssertEqual(ptr[5], 255) // green.g
        XCTAssertEqual(ptr[6], 0) // green.b

        XCTAssertEqual(ptr[8], 0) // black.r
        XCTAssertEqual(ptr[9], 0) // black.g
        XCTAssertEqual(ptr[10], 0) // black.b

        XCTAssertEqual(ptr[12], 255) // white.r
        XCTAssertEqual(ptr[13], 255) // white.g
        XCTAssertEqual(ptr[14], 255) // white.b
    }

    func testRejectsAShorterBufferThanWidthTimesHeightImplies() {
        let data = Data([0x00, 0xF8]) // only one pixel's worth of bytes
        XCTAssertNil(PlayerViewModel.image(fromRGB565: data, width: 2, height: 2))
    }

    func testRejectsZeroDimensions() {
        let data = Data([0x00, 0xF8, 0xE0, 0x07])
        XCTAssertNil(PlayerViewModel.image(fromRGB565: data, width: 0, height: 2))
        XCTAssertNil(PlayerViewModel.image(fromRGB565: data, width: 2, height: 0))
    }
}
