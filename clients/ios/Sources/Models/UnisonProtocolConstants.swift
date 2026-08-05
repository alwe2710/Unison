import Foundation

/// Wire-protocol bit constants, mirrored from core/include/unison/protocol.h's
/// `unison_key` (gba_buttons encoding) and `unison_button_bit`
/// (unison_extended_input encoding) -- same literal values as Android's
/// GbaStreamClient.kt companion object (see that file's own comment: these
/// are hardcoded to match the C enum rather than read from it at compile
/// time, same convention kept here for parity).
enum GbaKey {
    static let A = 1 << 0
    static let B = 1 << 1
    static let SELECT = 1 << 2
    static let START = 1 << 3
    static let RIGHT = 1 << 4
    static let LEFT = 1 << 5
    static let UP = 1 << 6
    static let DOWN = 1 << 7
    static let R = 1 << 8
    static let L = 1 << 9
}

enum ExtButtonBit {
    static let A = 1 << 0
    static let B = 1 << 1
    static let X = 1 << 2
    static let Y = 1 << 3
    static let L = 1 << 4
    static let R = 1 << 5
    static let ZL = 1 << 6
    static let ZR = 1 << 7
    static let SELECT = 1 << 8 // aka Minus (Wii U)
    static let START = 1 << 9 // aka Plus (Wii U)
    static let UP = 1 << 10
    static let DOWN = 1 << 11
    static let LEFT = 1 << 12
    static let RIGHT = 1 << 13
    static let HOME = 1 << 14
}
