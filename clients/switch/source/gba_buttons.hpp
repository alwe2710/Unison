#pragma once

#include <array>
#include <cstdint>

#include <borealis/core/input.hpp>

extern "C" {
#include "unison/protocol.h"
}

// One entry per physical GBA button, read by PlayerActivity's input
// polling. No per-button rebinding here (unlike
// clients/android/.../GbaButtons.kt) -- removed at the user's request, so
// this is just the fixed default GBA-button -> physical-controller-button
// mapping.
struct GbaButton {
    uint16_t bit;
    brls::ControllerButton defaultController;
};

inline constexpr std::array<GbaButton, 10> GBA_BUTTONS = { {
    { UNISON_KEY_UP, brls::BUTTON_UP },
    { UNISON_KEY_DOWN, brls::BUTTON_DOWN },
    { UNISON_KEY_LEFT, brls::BUTTON_LEFT },
    { UNISON_KEY_RIGHT, brls::BUTTON_RIGHT },
    { UNISON_KEY_SELECT, brls::BUTTON_BACK },
    { UNISON_KEY_START, brls::BUTTON_START },
    { UNISON_KEY_L, brls::BUTTON_LB },
    { UNISON_KEY_R, brls::BUTTON_RB },
    { UNISON_KEY_B, brls::BUTTON_B },
    { UNISON_KEY_A, brls::BUTTON_A },
} };
