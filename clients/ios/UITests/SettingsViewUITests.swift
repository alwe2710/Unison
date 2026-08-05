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

        // Not app.buttons["settingsButton"] -- that was true when this row
        // was RootView's own explicit Button, but RootView now uses
        // List(selection:) + .tag() for the sidebar (see that file's own
        // comment on why: it's what actually drives NavigationSplitView's
        // collapse-to-detail push on iPhone), and a List(selection:) row's
        // real underlying accessibility element type turned out not to be
        // .button (confirmed by CI: app.buttons[...] stopped finding it).
        // .any sidesteps needing to know/guess what it actually is.
        let settingsButton = app.descendants(matching: .any)["settingsButton"]
        // Diagnostic only, temporary: two rounds of guessing the right
        // query for RootView's List(selection:) sidebar row both failed in
        // CI (app.buttons[...], then descendants(matching: .any)) without
        // any way to see *why* from this environment (no local
        // Xcode/Simulator at all -- see clients/ios/README.md). Printing
        // the real accessibility tree XCUITest actually sees, captured in
        // CI's own console log, replaces guessing with an actual look.
        if !settingsButton.waitForExistence(timeout: 5) {
            print("=== UNISON DEBUG: settingsButton not found, full app.debugDescription ===")
            print(app.debugDescription)
            print("=== UNISON DEBUG end ===")
        }
        XCTAssertTrue(settingsButton.exists)
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
