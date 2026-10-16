#include "radio_home_controls.h"
#include <cassert>
#include <iostream>

int main() {
    using Action = RadioHomeAction;
    assert(radio_home_action(BSP_BTN_UP, BSP_BTN_CLICK) == Action::VolumeUp);
    assert(radio_home_action(BSP_BTN_DOWN, BSP_BTN_CLICK) == Action::VolumeDown);
    assert(radio_home_action(BSP_BTN_UP, BSP_BTN_DOUBLE) == Action::PreviousStation);
    assert(radio_home_action(BSP_BTN_DOWN, BSP_BTN_DOUBLE) == Action::NextStation);
    assert(radio_home_action(BSP_BTN_OK, BSP_BTN_CLICK) == Action::Toggle);
    assert(radio_home_action(BSP_BTN_OK, BSP_BTN_LONG) == Action::Settings);
    assert(radio_home_action(BSP_BTN_OK, BSP_BTN_DOUBLE) == Action::None);
    for (auto key : {BSP_BTN_UP, BSP_BTN_DOWN, BSP_BTN_OK}) {
        assert(radio_home_action(key, BSP_BTN_PRESS) == Action::None);
        assert(radio_home_action(key, BSP_BTN_RELEASE) == Action::None);
    }
    assert(radio_home_action(BSP_BTN_UP, BSP_BTN_LONG) == Action::None);
    assert(radio_home_action(BSP_BTN_DOWN, BSP_BTN_LONG) == Action::None);
    uint8_t volume = 55;
    assert(radio_volume_step(volume, true) == 60);
    assert(radio_volume_step(volume, false) == 50);
    for (int i = 0; i < 100; ++i) volume = radio_volume_step(volume, false);
    assert(volume == 10);
    for (int i = 0; i < 100; ++i) volume = radio_volume_step(volume, true);
    assert(volume == 100);
    assert(radio_volume_step(99, true) == 100);
    assert(radio_volume_step(11, false) == 10);
    std::cout << "Radio home single/double controls and volume bounds: PASS\n";
}
