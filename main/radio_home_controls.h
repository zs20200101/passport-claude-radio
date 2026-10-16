#pragma once

#include "bsp_button.h"
#include <algorithm>
#include <cstdint>

// Called only after permission handling, display wake and page routing.
// BSP emits SINGLE_CLICK and DOUBLE_CLICK exclusively; no app timer is needed.
enum class RadioHomeAction {
    None, VolumeUp, VolumeDown, PreviousStation, NextStation, Toggle, Settings,
};

constexpr RadioHomeAction radio_home_action(bsp_btn_t button, bsp_btn_ev_t event) {
    if (event == BSP_BTN_CLICK) {
        if (button == BSP_BTN_UP) return RadioHomeAction::VolumeUp;
        if (button == BSP_BTN_DOWN) return RadioHomeAction::VolumeDown;
        if (button == BSP_BTN_OK) return RadioHomeAction::Toggle;
    } else if (event == BSP_BTN_DOUBLE) {
        if (button == BSP_BTN_UP) return RadioHomeAction::PreviousStation;
        if (button == BSP_BTN_DOWN) return RadioHomeAction::NextStation;
    } else if (event == BSP_BTN_LONG && button == BSP_BTN_OK) {
        return RadioHomeAction::Settings;
    }
    return RadioHomeAction::None;
}

constexpr uint8_t radio_volume_step(uint8_t volume, bool up) {
    return static_cast<uint8_t>(std::clamp(static_cast<int>(volume) + (up ? 5 : -5),
                                           10, 100));
}
