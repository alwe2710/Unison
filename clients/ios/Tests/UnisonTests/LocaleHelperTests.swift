import XCTest
@testable import Unison

/// Direct port of clients/android/.../LocaleHelperTest.kt's
/// LocaleHelper.resolveLocaleTag() cases.
final class LocaleHelperTests: XCTestCase {
    func testExplicitPrefWinsOverSystemLanguage() {
        XCTAssertEqual(LocaleHelper.resolveLocaleTag(prefLanguage: "de", systemLanguageTag: "en"), "de")
        XCTAssertEqual(LocaleHelper.resolveLocaleTag(prefLanguage: "fr", systemLanguageTag: "es"), "fr")
    }

    func testSystemPrefResolvesToSupportedSystemLanguage() {
        XCTAssertEqual(
            LocaleHelper.resolveLocaleTag(prefLanguage: Prefs.languageSystem, systemLanguageTag: "de"), "de")
    }

    func testSystemPrefFallsBackToEnglishForUnsupportedSystemLanguage() {
        XCTAssertEqual(
            LocaleHelper.resolveLocaleTag(prefLanguage: Prefs.languageSystem, systemLanguageTag: "ja"), "en")
    }

    func testStaleUnsupportedPrefFallsBackThroughToSystem() {
        XCTAssertEqual(LocaleHelper.resolveLocaleTag(prefLanguage: "xx", systemLanguageTag: "fr"), "fr")
    }

    func testBothPrefAndSystemUnsupportedFallsBackToEnglish() {
        XCTAssertEqual(
            LocaleHelper.resolveLocaleTag(prefLanguage: Prefs.languageSystem, systemLanguageTag: ""), "en")
    }
}
