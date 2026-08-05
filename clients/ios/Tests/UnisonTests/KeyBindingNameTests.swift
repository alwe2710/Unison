import XCTest
@testable import Unison

final class KeyBindingNameTests: XCTestCase {
    func testLettersAndDigits() {
        XCTAssertEqual(KeyBindingName.name(forHIDUsageCode: 0x04), "A")
        XCTAssertEqual(KeyBindingName.name(forHIDUsageCode: 0x1D), "Z")
        XCTAssertEqual(KeyBindingName.name(forHIDUsageCode: 0x1E), "1")
        XCTAssertEqual(KeyBindingName.name(forHIDUsageCode: 0x27), "0")
    }

    func testNamedKeys() {
        XCTAssertEqual(KeyBindingName.name(forHIDUsageCode: 0x28), "Return")
        XCTAssertEqual(KeyBindingName.name(forHIDUsageCode: 0x2C), "Space")
        XCTAssertEqual(KeyBindingName.name(forHIDUsageCode: 0x52), "Up")
    }

    func testUnknownCodeFallsBackToNumericLabel() {
        XCTAssertEqual(KeyBindingName.name(forHIDUsageCode: 0x99), "Key 153")
    }
}
