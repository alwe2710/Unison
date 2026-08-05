import XCTest

/// Real Simulator UI interaction (XCUITest, launches the actual app
/// process) for SettingsView's on-screen-controls toggle -- iOS
/// counterpart to Android's SettingsActivityTest.kt (Robolectric-backed
/// Compose UI test), same "does a real tap on the real rendered control
/// actually flow through to persisted Prefs" property, just via a
/// different mechanism: SwiftUI/XCTest has no Robolectric-style plain-JVM
/// UI test path, so this genuinely needs a booted Simulator (see
/// clients/ios/README.md's own comment on why that's this client's only
/// verification loop at all, not just for UI tests specifically).
///
/// Deliberately checks a relative round-trip (toggle, assert changed,
/// toggle back, assert restored) rather than asserting an absolute
/// starting value -- UserDefaults persists across app launches within the
/// same Simulator, so a fresh test run's starting state depends on
/// whatever a previous run last left it as.
final class SettingsViewUITests: XCTestCase {
    override func setUpWithError() throws {
        continueAfterFailure = false
    }

    func testOnScreenControlsToggleRoundTrips() throws {
        let app = XCUIApplication()
        app.launch()

        let settingsButton = app.buttons["settingsButton"]
        XCTAssertTrue(settingsButton.waitForExistence(timeout: 5))
        settingsButton.tap()

        let toggle = app.switches["onScreenControlsToggle"]
        XCTAssertTrue(toggle.waitForExistence(timeout: 5))

        let initialValue = toggle.value as? String
        toggle.tap()
        // Real first CI run (2026-08-05): reading toggle.value synchronously
        // right after tap() raced the accessibility tree's own update and
        // saw the pre-tap value -- waitForValueChange polls instead of
        // asserting on a single synchronous snapshot, the standard robust
        // XCUITest pattern for this.
        XCTAssertTrue(Self.waitForValueChange(of: toggle, from: initialValue, timeout: 5),
                      "tapping the real rendered switch should flip its state")

        let toggledValue = toggle.value as? String
        toggle.tap()
        XCTAssertTrue(Self.waitForValueChange(of: toggle, from: toggledValue, timeout: 5),
                      "tapping it back should restore the original state")
        XCTAssertEqual(toggle.value as? String, initialValue)
    }

    private static func waitForValueChange(of element: XCUIElement, from previousValue: String?,
                                            timeout: TimeInterval) -> Bool {
        let predicate = NSPredicate { _, _ in (element.value as? String) != previousValue }
        let expectation = XCTNSPredicateExpectation(predicate: predicate, object: nil)
        return XCTWaiter().wait(for: [expectation], timeout: timeout) == .completed
    }
}
