package com.finlink.android

/** One entry per finlink_extended_input control (protocol.h's
 * finlink_button_bit bits, plus four synthetic stick-direction entries),
 * shared between PlayerActivity (physical key handling for a hasButtonsMode
 * session) and KeyBindingsActivity (key binding list), mirroring GbaButton's
 * own role for the plain gba_buttons encoding.
 *
 * BUTTON entries carry their finlink_button_bit in [bit] and get OR'd into
 * finlink_extended_input.buttons, exactly like GbaButton. The four STICK_*
 * entries are a keyboard/gamepad-only convenience with no wire bit of their
 * own ([bit] is unused, 0) -- held, they push the left stick to full
 * deflection on that axis (see PlayerActivity.sendCombinedExtendedInput's
 * digital-stick-from-keys math), the same "keyboard circle pad" convention
 * several other emulators offer alongside real analog input (VirtualStick,
 * for touch/drag).
 */
enum class ExtInputKind { BUTTON, STICK_UP, STICK_DOWN, STICK_LEFT, STICK_RIGHT }

data class ExtButton(val label: String, val kind: ExtInputKind, val bit: Int, val prefKey: String)

/** Only the hasButtonsMode controls with no Standard-Tasten (GBA_BUTTONS)
 * counterpart -- X/Y are real 3DS buttons the GBA overlay has no equivalent
 * for. A/B/L/R/Select/Start/Up/Down/Left/Right are deliberately NOT
 * repeated here even though a hasButtonsMode session also understands
 * them: they'd just be the same physical key bound a second time under a
 * different name, so PlayerActivity instead reuses the Standard-Tasten
 * binding directly for those (see GBA_PREFKEY_TO_EXT_BUTTON_BIT below).
 * Home is deliberately not bindable at all -- not needed for this use
 * case. */
val EXT_BUTTONS = listOf(
    ExtButton("X", ExtInputKind.BUTTON, GbaStreamClient.BUTTON_X, "X"),
    ExtButton("Y", ExtInputKind.BUTTON, GbaStreamClient.BUTTON_Y, "Y"),
)

/** GbaButton.prefKey (GbaButtons.kt) -> the finlink_button_bit it also
 * represents in a hasButtonsMode session, for the subset both encodings
 * share. PlayerActivity looks a bound physical key up here too (via
 * Prefs.sharedExtButtonBitsByKeyCode()) so one Standard-Tasten binding
 * drives both wire formats instead of asking the user to bind e.g. "A"
 * twice under two different screens. */
val GBA_PREFKEY_TO_EXT_BUTTON_BIT: Map<String, Int> = mapOf(
    "UP" to GbaStreamClient.BUTTON_UP,
    "DOWN" to GbaStreamClient.BUTTON_DOWN,
    "LEFT" to GbaStreamClient.BUTTON_LEFT,
    "RIGHT" to GbaStreamClient.BUTTON_RIGHT,
    "SELECT" to GbaStreamClient.BUTTON_SELECT,
    "START" to GbaStreamClient.BUTTON_START,
    "L" to GbaStreamClient.BUTTON_L,
    "R" to GbaStreamClient.BUTTON_R,
    "A" to GbaStreamClient.BUTTON_A,
    "B" to GbaStreamClient.BUTTON_B,
)

/** ZL/ZR and the left-stick directions -- only meaningful on the small
 * subset of stream types whose console actually has them (Cemu's
 * WIIU_GAMEPAD; no current server advertises a right stick, so there's no
 * STICK entry for it yet), kept in their own list so the UI can label this
 * group accordingly instead of implying every hasButtonsMode server uses
 * them. */
val EXT_BUTTONS_LIMITED = listOf(
    ExtButton("ZL", ExtInputKind.BUTTON, GbaStreamClient.BUTTON_ZL, "ZL"),
    ExtButton("ZR", ExtInputKind.BUTTON, GbaStreamClient.BUTTON_ZR, "ZR"),
    ExtButton("Stick hoch", ExtInputKind.STICK_UP, 0, "STICK_UP"),
    ExtButton("Stick runter", ExtInputKind.STICK_DOWN, 0, "STICK_DOWN"),
    ExtButton("Stick links", ExtInputKind.STICK_LEFT, 0, "STICK_LEFT"),
    ExtButton("Stick rechts", ExtInputKind.STICK_RIGHT, 0, "STICK_RIGHT"),
)
