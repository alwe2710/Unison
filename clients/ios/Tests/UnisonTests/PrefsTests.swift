import XCTest
@testable import Unison

/// Direct port of clients/android/.../PrefsTest.kt's
/// Prefs.defaultBilinear(for:) cases, plus real UserDefaults-backed
/// round-trip tests for the pieces Android's test left to Robolectric
/// (SettingsActivityTest) -- exercised here directly since a plain XCTest
/// target needs no Robolectric-equivalent for UserDefaults (it's already
/// a real, if sandboxed, on-device-ish store under XCTest).
final class PrefsTests: XCTestCase {
    private var suiteName: String!
    private var defaults: UserDefaults!
    private var prefs: Prefs!

    override func setUp() {
        super.setUp()
        suiteName = "PrefsTests.\(UUID().uuidString)"
        defaults = UserDefaults(suiteName: suiteName)
        prefs = Prefs(defaults: defaults)
    }

    override func tearDown() {
        defaults.removePersistentDomain(forName: suiteName)
        super.tearDown()
    }

    func testGcGbaLinkDefaultsToNearest() {
        XCTAssertFalse(Prefs.defaultBilinear(for: "GC_GBA_LINK"))
    }

    func testWiiuAndBottomScreenTypesDefaultToBilinear() {
        XCTAssertTrue(Prefs.defaultBilinear(for: "WIIU_GAMEPAD"))
        XCTAssertTrue(Prefs.defaultBilinear(for: "N3DS_BOTTOM_SCREEN"))
        XCTAssertTrue(Prefs.defaultBilinear(for: "NDS_BOTTOM_SCREEN"))
    }

    func testUnrecognizedStreamTypeDefaultsToNearest() {
        XCTAssertFalse(Prefs.defaultBilinear(for: "SOME_FUTURE_STREAM_TYPE"))
    }

    func testEmptyStreamTypeDefaultsToNearest() {
        XCTAssertFalse(Prefs.defaultBilinear(for: ""))
    }

    func testOnScreenControlsDefaultsToTrue() {
        XCTAssertTrue(prefs.onScreenControlsEnabled)
    }

    func testKeyBindingRoundTrip() {
        let up = GBA_BUTTONS.first { $0.prefKey == "UP" }!
        XCTAssertNil(prefs.keyBinding(for: up))
        prefs.setKeyBinding(42, for: up)
        XCTAssertEqual(prefs.keyBinding(for: up), 42)
        prefs.clearKeyBinding(for: up)
        XCTAssertNil(prefs.keyBinding(for: up))
    }

    func testKeyBindingsByKeyCodeOnlyIncludesBoundButtons() {
        let up = GBA_BUTTONS.first { $0.prefKey == "UP" }!
        prefs.setKeyBinding(7, for: up)
        let map = prefs.keyBindingsByKeyCode()
        XCTAssertEqual(map[7], up.bit)
        XCTAssertEqual(map.count, 1)
    }

    func testControllerBindingRoundTrip() {
        let up = GBA_BUTTONS.first { $0.prefKey == "UP" }!
        XCTAssertNil(prefs.controllerBinding(for: up))
        prefs.setControllerBinding(.dpadUp, for: up)
        XCTAssertEqual(prefs.controllerBinding(for: up), .dpadUp)
        prefs.clearControllerBinding(for: up)
        XCTAssertNil(prefs.controllerBinding(for: up))
    }

    func testControllerBindingsByElementOnlyIncludesBoundButtons() {
        let up = GBA_BUTTONS.first { $0.prefKey == "UP" }!
        prefs.setControllerBinding(.dpadUp, for: up)
        let map = prefs.controllerBindingsByElement()
        XCTAssertEqual(map[.dpadUp], up.bit)
        XCTAssertEqual(map.count, 1)
    }

    /// A keyboard binding and a controller binding for the *same* button
    /// are independent -- setting one must not disturb the other, matching
    /// ControllerInputHandler's own "both active at once" comment.
    func testKeyAndControllerBindingsAreIndependentForTheSameButton() {
        let a = GBA_BUTTONS.first { $0.prefKey == "A" }!
        prefs.setKeyBinding(9, for: a)
        prefs.setControllerBinding(.buttonA, for: a)
        XCTAssertEqual(prefs.keyBinding(for: a), 9)
        XCTAssertEqual(prefs.controllerBinding(for: a), .buttonA)
        prefs.clearControllerBinding(for: a)
        XCTAssertEqual(prefs.keyBinding(for: a), 9, "clearing the controller binding must not clear the keyboard one")
        XCTAssertNil(prefs.controllerBinding(for: a))
    }

    func testExtControllerBindingRoundTrip() {
        let x = EXT_BUTTONS.first { $0.prefKey == "X" }!
        XCTAssertNil(prefs.controllerBinding(for: x))
        prefs.setControllerBinding(.buttonY, for: x)
        XCTAssertEqual(prefs.controllerBinding(for: x), .buttonY)
        let map = prefs.extControllerBindingsByElement()
        XCTAssertEqual(map[.buttonY]?.prefKey, "X")
    }

    func testSharedExtButtonBitsByControllerElementUsesStandardBindings() {
        let up = GBA_BUTTONS.first { $0.prefKey == "UP" }!
        prefs.setControllerBinding(.dpadUp, for: up)
        let map = prefs.sharedExtButtonBitsByControllerElement()
        XCTAssertEqual(map[.dpadUp], ExtButtonBit.UP)
    }

    func testBilinearOverridePersistsPerStreamType() {
        XCTAssertEqual(prefs.bilinear(for: "GC_GBA_LINK"), false)
        prefs.setBilinear(true, for: "GC_GBA_LINK")
        XCTAssertEqual(prefs.bilinear(for: "GC_GBA_LINK"), true)
        // A different stream_type is unaffected.
        XCTAssertEqual(prefs.bilinear(for: "WIIU_GAMEPAD"), true)
    }
}
