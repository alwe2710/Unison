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
        let toggledValue = toggle.value as? String
        XCTAssertNotEqual(initialValue, toggledValue, "tapping the real rendered switch should flip its state")

        toggle.tap()
        let restoredValue = toggle.value as? String
        XCTAssertEqual(initialValue, restoredValue, "tapping it back should restore the original state")
    }
}
