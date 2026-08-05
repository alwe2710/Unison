import SwiftUI

/// Matches assets/logo/unison-logo.png (dark navy background, cyan glow) --
/// same color constants as clients/android/.../Theme.kt's UnisonDarkScheme,
/// applied via a single `.tint()` at the app root (UnisonApp.swift) rather
/// than a full custom color-scheme system: SwiftUI has no direct
/// equivalent to Material3's dynamicColor-on-Android-12+ story, so this
/// only carries over the brand accent, not Theme.kt's whole
/// light/dark/dynamic matrix.
enum UnisonTheme {
    static let cyan = Color(red: 0x4D / 255, green: 0xD8 / 255, blue: 0xE8 / 255)
    static let cyanMuted = Color(red: 0x1E / 255, green: 0x8F / 255, blue: 0xA6 / 255)
    static let navy = Color(red: 0x0A / 255, green: 0x11 / 255, blue: 0x28 / 255)
    static let navyLight = Color(red: 0x16 / 255, green: 0x21 / 255, blue: 0x3E / 255)
}
