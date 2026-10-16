#include "radio_catalog.h"
#include "radio_http.h"
#include "claude_bridge.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr char kTag[] = "radio_catalog";
constexpr char kLocationUrl[] =
    "http://ip-api.com/json/?fields=status,countryCode,regionName,city";
constexpr char kBackupLocationUrl[] =
    "https://ipwho.is/?fields=success,country_code,region,city";
constexpr char kDirectoryBase[] =
    "http://de1.api.radio-browser.info/json/stations/search";
constexpr std::size_t kResponseCapacity = 24576;

struct CityTranslation {
    const char *english;
    const char *chinese;
};

// ip-api returns English city names. Radio Browser's Chinese entries are much
// easier to find by their Chinese city name, so map the most common cities and
// fall back to a province/state query for every other place.
constexpr CityTranslation kChineseCities[] = {
    {"Beijing", "北京"},       {"Shanghai", "上海"},
    {"Tianjin", "天津"},       {"Chongqing", "重庆"},
    {"Guangzhou", "广州"},     {"Shenzhen", "深圳"},
    {"Dongguan", "东莞"},      {"Foshan", "佛山"},
    {"Zhuhai", "珠海"},        {"Huizhou", "惠州"},
    {"Zhongshan", "中山"},     {"Shantou", "汕头"},
    {"Hangzhou", "杭州"},      {"Ningbo", "宁波"},
    {"Wenzhou", "温州"},       {"Jiaxing", "嘉兴"},
    {"Shaoxing", "绍兴"},      {"Jinhua", "金华"},
    {"Nanjing", "南京"},       {"Suzhou", "苏州"},
    {"Wuxi", "无锡"},          {"Changzhou", "常州"},
    {"Nantong", "南通"},       {"Xuzhou", "徐州"},
    {"Chengdu", "成都"},       {"Mianyang", "绵阳"},
    {"Wuhan", "武汉"},         {"Yichang", "宜昌"},
    {"Changsha", "长沙"},      {"Zhengzhou", "郑州"},
    {"Luoyang", "洛阳"},       {"Jinan", "济南"},
    {"Qingdao", "青岛"},       {"Yantai", "烟台"},
    {"Shenyang", "沈阳"},      {"Dalian", "大连"},
    {"Harbin", "哈尔滨"},      {"Changchun", "长春"},
    {"Shijiazhuang", "石家庄"}, {"Taiyuan", "太原"},
    {"Xi'an", "西安"},         {"Xian", "西安"},
    {"Kunming", "昆明"},       {"Guiyang", "贵阳"},
    {"Nanning", "南宁"},       {"Haikou", "海口"},
    {"Fuzhou", "福州"},        {"Xiamen", "厦门"},
    {"Nanchang", "南昌"},      {"Hefei", "合肥"},
    {"Lanzhou", "兰州"},       {"Urumqi", "乌鲁木齐"},
    {"Hohhot", "呼和浩特"},    {"Yinchuan", "银川"},
    {"Xining", "西宁"},        {"Lhasa", "拉萨"},
};

bool starts_with(const char *text, const char *prefix) {
    return text && prefix && std::strncmp(text, prefix, std::strlen(prefix)) == 0;
}

bool contains(const char *text, const char *needle) {
    return text && needle && std::strstr(text, needle) != nullptr;
}


const char *json_string(cJSON *object, const char *name) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

void copy_text(char *target, std::size_t capacity, const char *source) {
    if (!target || capacity == 0) return;
    std::snprintf(target, capacity, "%s", source ? source : "");
}

bool locate(RadioLocation *location) {
    char *response = nullptr;
    bool success = false;
    if (radio_http_get(kLocationUrl, response, 2048, nullptr)) {
        cJSON *root = cJSON_Parse(response);
        if (root) {
            const char *status = json_string(root, "status");
            const char *country = json_string(root, "countryCode");
            const char *region = json_string(root, "regionName");
            const char *city = json_string(root, "city");
            if (std::strcmp(status, "success") == 0 && country[0] &&
                (city[0] || region[0])) {
                copy_text(location->country_code, sizeof(location->country_code), country);
                copy_text(location->region, sizeof(location->region), region);
                copy_text(location->city, sizeof(location->city), city);
                success = true;
            }
            cJSON_Delete(root);
        }
    }
    std::free(response);
    response = nullptr;
    if (!success && radio_http_get(kBackupLocationUrl, response, 2048, nullptr)) {
        cJSON *root = cJSON_Parse(response);
        if (root) {
            cJSON *ok = cJSON_GetObjectItemCaseSensitive(root, "success");
            const char *country = json_string(root, "country_code");
            const char *region = json_string(root, "region");
            const char *city = json_string(root, "city");
            if (cJSON_IsTrue(ok) && country[0] && (city[0] || region[0])) {
                copy_text(location->country_code, sizeof(location->country_code), country);
                copy_text(location->region, sizeof(location->region), region);
                copy_text(location->city, sizeof(location->city), city);
                success = true;
            }
            cJSON_Delete(root);
        }
    }
    std::free(response);
    return success;
}

void url_encode(const char *source, char *target, std::size_t capacity) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::size_t out = 0;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(source);
         p && *p && out + 1 < capacity; ++p) {
        const unsigned char value = *p;
        if (std::isalnum(value) || value == '-' || value == '_' || value == '.' ||
            value == '~') {
            target[out++] = static_cast<char>(value);
        } else if (out + 3 < capacity) {
            target[out++] = '%';
            target[out++] = kHex[value >> 4];
            target[out++] = kHex[value & 0x0F];
        } else {
            break;
        }
    }
    target[out] = '\0';
}

const char *translated_city(const RadioLocation &location) {
    if (std::strcmp(location.country_code, "CN") != 0) return location.city;
    for (const auto &entry : kChineseCities) {
        if (std::strcmp(entry.english, location.city) == 0) return entry.chinese;
    }
    return nullptr;
}

void normalize_stream_url(const char *source, char *target, std::size_t capacity) {
    if (starts_with(source, "https://") &&
        (contains(source, ".qtfm.cn/") || contains(source, ".qingting.fm/"))) {
        std::snprintf(target, capacity, "http://%s", source + 8);
    } else {
        copy_text(target, capacity, source);
    }
}

bool duplicate_url(const RadioStation *stations, std::size_t count, const char *url) {
    for (std::size_t i = 0; i < count; ++i) {
        if (std::strcmp(stations[i].url, url) == 0) return true;
    }
    return false;
}

// Chinese FM broadcasting occupies 87.0-108.0 MHz. Station names in the public
// directory spell the frequency in three ways, so they are tried in order of
// confidence: an explicit "FM" prefix, a bare decimal such as "94.7", and
// finally a compact form such as "971" that stations use as a brand. The last
// form is the least reliable, so it is only accepted when the digits are not
// glued to other numbers and still land inside the broadcast band.
constexpr uint16_t kBandLowDecihz = 870;
constexpr uint16_t kBandHighDecihz = 1080;

bool in_band(long decihz) {
    return decihz >= kBandLowDecihz && decihz <= kBandHighDecihz;
}

bool is_digit(char value) { return value >= '0' && value <= '9'; }

// Reads "94.7" or "1017" style digits at `text` and returns tenths of a MHz.
// Digit runs without a decimal point are ambiguous: "971" is 97.1 MHz while
// "101" is 101.0 MHz. Both readings are tried and the one inside the broadcast
// band wins, checking the tenths reading first because it is far more common.
long read_decihz(const char *text, std::size_t *consumed) {
    std::size_t whole = 0;
    while (is_digit(text[whole])) ++whole;
    if (whole < 2 || whole > 4) return 0;

    long value = 0;
    for (std::size_t i = 0; i < whole; ++i) value = value * 10 + (text[i] - '0');

    if (text[whole] == '.' && is_digit(text[whole + 1])) {
        if (whole > 3 || is_digit(text[whole + 2])) return 0;
        *consumed = whole + 2;
        return value * 10 + (text[whole + 1] - '0');
    }

    *consumed = whole;
    if (whole == 2) return value * 10;      // "94" is 94.0 MHz
    if (in_band(value)) return value;       // "971" is 97.1 MHz
    if (in_band(value * 10)) return value * 10;  // "101" is 101.0 MHz
    return value;
}

uint16_t parse_frequency(const char *name) {
    if (!name) return 0;

    for (const char *p = name; p[0] && p[1]; ++p) {
        if ((p[0] != 'F' && p[0] != 'f') || (p[1] != 'M' && p[1] != 'm')) continue;
        const char *digits = p + 2;
        while (*digits == ' ') ++digits;
        std::size_t consumed = 0;
        const long value = read_decihz(digits, &consumed);
        if (in_band(value)) return static_cast<uint16_t>(value);
    }

    for (const char *p = name; *p; ++p) {
        if (!is_digit(*p)) continue;
        const bool starts_run = p == name || (!is_digit(p[-1]) && p[-1] != '.');
        if (!starts_run) continue;
        std::size_t consumed = 0;
        const long value = read_decihz(p, &consumed);
        if (consumed && in_band(value)) return static_cast<uint16_t>(value);
        while (is_digit(p[1])) ++p;
    }
    return 0;
}

std::size_t parse_stations(const char *json, RadioStation *stations, std::size_t capacity) {
    cJSON *root = cJSON_Parse(json);
    if (!root || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return 0;
    }

    std::size_t count = 0;
    cJSON *entry = nullptr;
    cJSON_ArrayForEach(entry, root) {
        if (count >= capacity || !cJSON_IsObject(entry)) break;
        const char *raw_name = json_string(entry, "name");
        const char *raw_url = json_string(entry, "url_resolved");
        if (!raw_url[0]) raw_url = json_string(entry, "url");
        cJSON *last_ok = cJSON_GetObjectItemCaseSensitive(entry, "lastcheckok");
        cJSON *hls = cJSON_GetObjectItemCaseSensitive(entry, "hls");
        if (!raw_name[0] || !raw_url[0] || !contains(raw_url, ".mp3") ||
            (cJSON_IsNumber(last_ok) && last_ok->valueint != 1) ||
            (cJSON_IsNumber(hls) && hls->valueint != 0)) {
            continue;
        }

        RadioStation candidate = {};
        normalize_stream_url(raw_url, candidate.url, sizeof(candidate.url));
        if ((!starts_with(candidate.url, "http://") &&
             !starts_with(candidate.url, "https://")) ||
            duplicate_url(stations, count, candidate.url)) {
            continue;
        }
        candidate.frequency_decihz = parse_frequency(raw_name);
        copy_text(candidate.name, sizeof(candidate.name), raw_name);
        // Truncate only at a UTF-8 character boundary for long directory names.
        const std::size_t name_size = std::strlen(candidate.name);
        if (std::strlen(raw_name) >= sizeof(candidate.name)) {
            std::size_t end = name_size;
            while (end && (static_cast<unsigned char>(raw_name[end]) & 0xc0) == 0x80) --end;
            candidate.name[end] = '\0';
        }
        copy_text(candidate.description, sizeof(candidate.description),
                  "城市电台 · 网络直播");
        stations[count++] = candidate;
    }
    cJSON_Delete(root);
    return count;
}

std::size_t query_directory(const RadioLocation &location, const char *field,
                            const char *value, RadioStation *stations,
                            std::size_t capacity) {
    char country[16] = {};
    char encoded[192] = {};
    url_encode(location.country_code, country, sizeof(country));
    url_encode(value, encoded, sizeof(encoded));
    char url[512] = {};
    std::snprintf(url, sizeof(url),
                  "%s?countrycode=%s&%s=%s&codec=MP3&hidebroken=true"
                  "&order=clickcount&reverse=true&limit=%u",
                  kDirectoryBase, country, field, encoded,
                  static_cast<unsigned>(std::min<std::size_t>(capacity + 3, 12)));

    char *response = nullptr;
    std::size_t count = 0;
    if (radio_http_get(url, response, kResponseCapacity, nullptr)) {
        count = parse_stations(response, stations, capacity);
    }
    std::free(response);
    return count;
}

}  // namespace

std::size_t radio_catalog_discover(RadioStation *stations, std::size_t capacity,
                                   RadioLocation *location) {
    if (!stations || capacity == 0 || !location) return 0;
    *location = {};
    if (!locate(location)) {
        ESP_LOGW(kTag, "Unable to determine network location");
        return 0;
    }
    ESP_LOGI(kTag, "Detected location: %s, %s, %s", location->city,
             location->region, location->country_code);

    std::size_t count = 0;
    const char *city_query = translated_city(*location);
    if (city_query && city_query[0]) {
        count = query_directory(*location, "name", city_query, stations, capacity);
        ESP_LOGI(kTag, "City search '%s' returned %u playable stations", city_query,
                 static_cast<unsigned>(count));
    } else if (std::strcmp(location->country_code, "CN") != 0 && location->city[0]) {
        count = query_directory(*location, "name", location->city, stations, capacity);
        ESP_LOGI(kTag, "City search '%s' returned %u playable stations", location->city,
                 static_cast<unsigned>(count));
    }

    if (count == 0 && location->region[0]) {
        count = query_directory(*location, "state", location->region, stations, capacity);
        ESP_LOGI(kTag, "Region search '%s' returned %u playable stations",
                 location->region, static_cast<unsigned>(count));
    }
    return count;
}

std::size_t radio_catalog_discover_city(const char *city, RadioStation *stations,
                                        std::size_t capacity,
                                        RadioLocation *location) {
    if (!city || !city[0] || !stations || capacity == 0 || !location) return 0;
    *location = {};
    copy_text(location->country_code, sizeof(location->country_code), "CN");
    copy_text(location->city, sizeof(location->city), city);
    copy_text(location->region, sizeof(location->region), city);

    static const RadioStation national[] = {
        {"中国之声", "央广 · 网络直播", "http://lhttp.qtfm.cn/live/15318317/64k.mp3", 0, false},
    };
    static const RadioStation guangdong[] = {
        {"广东音乐之声", "广东 · 网络直播", "http://lhttp.qtfm.cn/live/1260/64k.mp3", 0, false},
        {"广东珠江经济台", "广东 · 网络直播", "http://lhttp.qtfm.cn/live/1259/64k.mp3", 0, false},
        {"广东新闻广播", "广东 · 网络直播", "http://lhttp.qtfm.cn/live/1254/64k.mp3", 0, false},
        {"广东股市广播", "广东 · 网络直播", "http://lhttp.qtfm.cn/live/4847/64k.mp3", 0, false},
        {"广东交通之声", "广东 · 网络直播", "http://lhttp.qtfm.cn/live/1262/64k.mp3", 0, false},
        {"广东城市之声", "广东 · 网络直播", "http://lhttp.qtfm.cn/live/469/64k.mp3", 0, false},
        {"广东文体广播", "广东 · 网络直播", "http://lhttp.qtfm.cn/live/471/64k.mp3", 0, false},
        {"广东南粤之声", "广东 · 网络直播", "http://lhttp.qtfm.cn/live/470/64k.mp3", 0, false},
    };
    const RadioStation *preset = nullptr;
    std::size_t preset_count = 0;
    if (std::strcmp(city, "央广") == 0) {
        preset = national; preset_count = sizeof(national) / sizeof(national[0]);
    } else if (std::strcmp(city, "广东") == 0) {
        preset = guangdong; preset_count = sizeof(guangdong) / sizeof(guangdong[0]);
    }
    if (preset) {
        const auto count = std::min(capacity, preset_count);
        std::copy_n(preset, count, stations);
        return count;
    }
    if (std::strcmp(city, "凤凰资讯") == 0) {
        RadioStation phoenix = {};
        copy_text(phoenix.name, sizeof(phoenix.name), "凤凰资讯");
        copy_text(phoenix.description, sizeof(phoenix.description), "凤凰资讯台 · 电脑转播");
        if (!claude_bridge_radio_url(phoenix.url, sizeof(phoenix.url))) {
            ESP_LOGW(kTag, "Phoenix requires the paired LAN companion");
            return 0;
        }
        stations[0] = phoenix;
        return 1;
    }
    if (std::strcmp(city, "香港") == 0 || std::strcmp(city, "在线听书") == 0) return 0;
    const std::size_t count =
        query_directory(*location, "name", city, stations, capacity);
    ESP_LOGI(kTag, "Manual city search '%s' returned %u playable stations", city,
             static_cast<unsigned>(count));
    return count;
}
