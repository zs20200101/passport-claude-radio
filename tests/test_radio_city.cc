#include "radio_city.h"
#include <cassert>
#include <cstring>

int main() {
    assert(kRadioCityCount == 12);
    assert(radio_city_index("AUTO") == 0);
    assert(radio_city_index("CHENGDU") == 7); // existing NVS index retained
    assert(radio_city_index("昆明") == 8);
    assert(radio_city_index("央广") == 9);
    assert(radio_city_index("广东") == 10);
    assert(radio_city_index("凤凰资讯") == 11);
    assert(radio_city_index("香港") == -1);
    assert(radio_city_index("在线听书") == -1);
    for (uint8_t i = 0; i <= 10; ++i) assert(radio_city_restore(i, 0) == i);
    assert(radio_city_restore(11, 0) == 8);
    assert(radio_city_restore(12, 0) == 8);
    assert(radio_city_restore(11, 2) == 11);
    assert(radio_city_restore(255, 2) == 0);
    assert(radio_city_index(nullptr) == -1);
    assert(radio_city_index("") == -1);
    assert(radio_city_index("昆明 extra") == -1);
    for (std::size_t selected = 0; selected < kRadioCityCount; ++selected) {
        const auto first = radio_city_first_row(selected);
        assert(first <= selected);
        assert(first + kRadioCityVisibleRows <= kRadioCityCount);
        assert(selected - first < kRadioCityVisibleRows);
        assert(34 + 7 * 30 + 27 < 283); // last row above footer
        assert(radio_city_index(kRadioCities[selected].display) == static_cast<int>(selected));
    }
}
