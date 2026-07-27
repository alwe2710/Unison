#pragma once

#include <array>
#include <cstdint>

#include <borealis/core/input.hpp>

extern "C" {
#include "finlink/protocol.h"
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
    { FINLINK_KEY_UP, brls::BUTTON_UP },
    { FINLINK_KEY_DOWN, brls::BUTTON_DOWN },
    { FINLINK_KEY_LEFT, brls::BUTTON_LEFT },
    { FINLINK_KEY_RIGHT, brls::BUTTON_RIGHT },
    { FINLINK_KEY_SELECT, brls::BUTTON_BACK },
    { FINLINK_KEY_START, brls::BUTTON_START },
    { FINLINK_KEY_L, brls::BUTTON_LB },
    { FINLINK_KEY_R, brls::BUTTON_RB },
    { FINLINK_KEY_B, brls::BUTTON_B },
    { FINLINK_KEY_A, brls::BUTTON_A },
} };
