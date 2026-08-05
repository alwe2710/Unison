package com.unison.android

/** One entry per physical GBA button, shared between PlayerActivity (touch
 * row + physical key handling) and SettingsActivity (key binding list) so
 * both stay in sync with a single source of truth. */
data class GbaButton(val label: String, val bit: Int, val prefKey: String)

val GBA_BUTTONS = listOf(
    GbaButton("Up", GbaStreamClient.KEY_UP, "UP"),
    GbaButton("Down", GbaStreamClient.KEY_DOWN, "DOWN"),
    GbaButton("Left", GbaStreamClient.KEY_LEFT, "LEFT"),
    GbaButton("Right", GbaStreamClient.KEY_RIGHT, "RIGHT"),
    GbaButton("Select", GbaStreamClient.KEY_SELECT, "SELECT"),
    GbaButton("Start", GbaStreamClient.KEY_START, "START"),
    GbaButton("L", GbaStreamClient.KEY_L, "L"),
    GbaButton("R", GbaStreamClient.KEY_R, "R"),
    GbaButton("B", GbaStreamClient.KEY_B, "B"),
    GbaButton("A", GbaStreamClient.KEY_A, "A"),
)
