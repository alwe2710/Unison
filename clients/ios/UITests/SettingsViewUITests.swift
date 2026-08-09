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

    // Was TEMPORARILY DISABLED (2026-08-05): five straight CI rounds of
    // trying to pin down exactly which element/query reliably reaches
    // RootView's List(selection:) sidebar row (app.buttons[...], then
    // .any+.firstMatch, then .cells[...], then .any+isHittable-filter)
    // each hit a different real failure -- not found / found-but-not-
    // hittable / not found again -- without ever landing on something
    // solid. Re-enabled now that RootView's sidebar is gone entirely
    // (reverted to a plain gear-icon toolbar button on MenuView, see that
    // file's own comment) -- the flaky List(selection:) row this was
    // chasing doesn't exist anymore, and a toolbar button's own
    // accessibility identifier is unambiguous.
    func testOnScreenControlsToggleRoundTrips() throws {
        let app = XCUIApplication()
        app.launch()

        // Settings is a direct NavigationLink in MenuView's own toolbar
        // now (top-right gear icon) -- no sidebar to reach first.
        let settingsButton = app.buttons["settingsButton"]
        XCTAssertTrue(settingsButton.waitForExistence(timeout: 5))
        settingsButton.tap()

        let toggle = app.switches["onScreenControlsToggle"]
        XCTAssertTrue(toggle.waitForExistence(timeout: 5))

        let initialValue = toggle.value as? String
        Self.tapSwitch(toggle)
        // Real first two CI runs (2026-08-05): plain toggle.tap() never
        // registered at all, even waiting up to 5s for the value to
        // change -- SwiftUI merges a Form row's label + Toggle into one
        // accessibility element spanning the row's full width, so tap()'s
        // default "center of the element" coordinate lands on the label
        // text, not the actual switch control (right-aligned within that
        // same row). tapSwitch() below taps near the element's own
        // trailing edge instead, where the real control renders
        // regardless of whether XCUITest reports the merged row's frame
        // or just the switch's.
        XCTAssertTrue(Self.waitForValueChange(of: toggle, from: initialValue, timeout: 5),
                      "tapping the real rendered switch should flip its state")

        let toggledValue = toggle.value as? String
        Self.tapSwitch(toggle)
        XCTAssertTrue(Self.waitForValueChange(of: toggle, from: toggledValue, timeout: 5),
                      "tapping it back should restore the original state")
        XCTAssertEqual(toggle.value as? String, initialValue)
    }

    private static func tapSwitch(_ element: XCUIElement) {
        element.coordinate(withNormalizedOffset: CGVector(dx: 0.92, dy: 0.5)).tap()
    }

    private static func waitForValueChange(of element: XCUIElement, from previousValue: String?,
                                            timeout: TimeInterval) -> Bool {
        let predicate = NSPredicate { _, _ in (element.value as? String) != previousValue }
        let expectation = XCTNSPredicateExpectation(predicate: predicate, object: nil)
        return XCTWaiter().wait(for: [expectation], timeout: timeout) == .completed
    }
}
