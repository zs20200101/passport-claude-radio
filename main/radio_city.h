#pragma once
#include <stdbool.h>

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#include <cstring>

struct RadioCityChoice {
    const char *query;
    const char *display;
    const char *label;
};

// Keep existing city/CNR/Guangdong indices stable; retired media indices migrate.
inline constexpr RadioCityChoice kRadioCities[] = {
    {nullptr, "AUTO", "自动定位"},
    {"北京", "BEIJING", "北京"},
    {"上海", "SHANGHAI", "上海"},
    {"广州", "GUANGZHOU", "广州"},
    {"深圳", "SHENZHEN", "深圳"},
    {"长沙", "CHANGSHA", "长沙"},
    {"杭州", "HANGZHOU", "杭州"},
    {"成都", "CHENGDU", "成都"},
    {"昆明", "昆明", "昆明"},
    {"央广", "央广", "央广"},
    {"广东", "广东", "广东"},
    {"凤凰资讯", "凤凰资讯", "凤凰资讯"},
};
inline constexpr std::size_t kRadioCityCount = sizeof(kRadioCities) / sizeof(kRadioCities[0]);
inline constexpr std::size_t kRadioCityVisibleRows = 8;
// Versioned NVS migration: old 11/12 meant Hong Kong/listening, not Phoenix.
inline constexpr uint8_t kRadioCatalogVersion = 2;
inline constexpr std::size_t radio_city_restore(uint8_t saved, uint8_t version) {
    if (version < kRadioCatalogVersion && saved >= 11) return 8;
    return saved < kRadioCityCount ? saved : 0;
}
inline constexpr std::size_t radio_city_first_row(std::size_t selected) {
    return selected < kRadioCityVisibleRows ? 0 : selected + 1 - kRadioCityVisibleRows;
}
inline int radio_city_index(const char *name) {
    if (!name || !*name) return -1;
    for (std::size_t i = 0; i < kRadioCityCount; ++i) {
        if (std::strcmp(name, kRadioCities[i].display) == 0 ||
            (kRadioCities[i].query && std::strcmp(name, kRadioCities[i].query) == 0)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}
extern "C" {
#endif
// Physical USB configuration: queues a city selection, never a button/approval.
// True means queued. The input worker persists it and starts discovery.
bool radio_city_select(const char *name);
#ifdef __cplusplus
}
#endif
