#include "radio_media.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
using View = std::string_view;
constexpr auto missing = View::npos;

// RSS descriptions commonly contain HTML/closing tags inside CDATA. Never
// interpret those as item boundaries or fields, and ignore XML comments.
std::size_t markup(View xml, View wanted, std::size_t from = 0) {
    while (from < xml.size()) {
        const auto at = xml.find('<', from);
        if (at == missing) return missing;
        if (xml.substr(at, 9) == "<![CDATA[") {
            const auto end = xml.find("]]>", at + 9);
            if (end == missing) return missing;
            from = end + 3;
        } else if (xml.substr(at, 4) == "<!--") {
            const auto end = xml.find("-->", at + 4);
            if (end == missing) return missing;
            from = end + 3;
        } else if (xml.substr(at, wanted.size()) == wanted) {
            return at;
        } else {
            from = at + 1;
        }
    }
    return missing;
}

void unicode(std::string &out, unsigned long point) {
    if (point == 0 || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) return;
    if (point < 0x80) out += static_cast<char>(point);
    else if (point < 0x800) {
        out += static_cast<char>(0xc0 | (point >> 6));
        out += static_cast<char>(0x80 | (point & 63));
    } else if (point < 0x10000) {
        out += static_cast<char>(0xe0 | (point >> 12));
        out += static_cast<char>(0x80 | ((point >> 6) & 63));
        out += static_cast<char>(0x80 | (point & 63));
    } else {
        out += static_cast<char>(0xf0 | (point >> 18));
        out += static_cast<char>(0x80 | ((point >> 12) & 63));
        out += static_cast<char>(0x80 | ((point >> 6) & 63));
        out += static_cast<char>(0x80 | (point & 63));
    }
}

std::string unescape(View value) {
    std::string out;
    for (std::size_t i = 0; i < value.size() && out.size() < 1024;) {
        if (value.substr(i, 9) == "<![CDATA[") {
            const auto end = value.find("]]>", i + 9);
            if (end == missing) break;
            out.append(value.substr(i + 9, std::min<std::size_t>(end - i - 9, 1024 - out.size())));
            i = end + 3;
        } else if (value[i] == '&') {
            const auto end = value.find(';', i + 1);
            if (end == missing || end - i > 12) { out += value[i++]; continue; }
            const auto entity = value.substr(i + 1, end - i - 1);
            if (entity == "amp") out += '&';
            else if (entity == "lt") out += '<';
            else if (entity == "gt") out += '>';
            else if (entity == "quot") out += '"';
            else if (entity == "apos") out += '\'';
            else if (entity.size() > 1 && entity[0] == '#') {
                const bool hex = entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X');
                const std::string digits(entity.substr(hex ? 2 : 1));
                char *tail = nullptr; errno = 0;
                const auto point = std::strtoul(digits.c_str(), &tail, hex ? 16 : 10);
                if (!errno && tail != digits.c_str() && *tail == '\0') unicode(out, point);
            }
            i = end + 1;
        } else {
            out += value[i++];
        }
    }
    return out;
}

View attribute(View tag, View name) {
    // Parse quoted attributes; name matching inside another attribute's value
    // (for example title="url=...") must never supply a media URL.
    auto at = tag.find_first_of(" \t\r\n");
    while (at != missing && at < tag.size()) {
        at = tag.find_first_not_of(" \t\r\n/>", at);
        if (at == missing) break;
        const auto end = tag.find_first_of("= \t\r\n", at);
        if (end == missing) break;
        const auto key = tag.substr(at, end - at);
        auto eq = tag.find_first_not_of(" \t\r\n", end);
        if (eq == missing || tag[eq] != '=') break;
        auto quote = tag.find_first_not_of(" \t\r\n", eq + 1);
        if (quote == missing || (tag[quote] != '\'' && tag[quote] != '"')) break;
        const auto close = tag.find(tag[quote], quote + 1);
        if (close == missing) break;
        if (key == name) return tag.substr(quote + 1, close - quote - 1);
        at = close + 1;
    }
    return {};
}

void title_copy(char *target, std::size_t capacity, std::string value) {
    for (char &c : value) if (static_cast<unsigned char>(c) < 32 || c == 127) c = ' ';
    auto length = std::min(value.size(), capacity - 1);
    while (length && length < value.size() &&
           (static_cast<unsigned char>(value[length]) & 0xc0) == 0x80) --length;
    std::memcpy(target, value.data(), length); target[length] = '\0';
}

bool media_url(const std::string &url) {
    const auto scheme = url.find("://");
    if (scheme == missing || (url.substr(0, scheme) != "http" && url.substr(0, scheme) != "https")) return false;
    const auto slash = url.find('/', scheme + 3);
    if (slash == missing || slash == scheme + 3 || url.find('@', scheme + 3) < slash) return false;
    for (unsigned char c : url) if (c <= 32 || c == 127 || c == '\\') return false;
    return true;
}
}  // namespace

static std::size_t parse_items(std::string_view rss, RadioStation *stations,
                               std::size_t capacity, std::size_t count) {
    if (!stations || !capacity || rss.find("<!DOCTYPE") != missing || rss.find("<!ENTITY") != missing) return 0;
    std::size_t cursor = 0;
    while (count < capacity) {
        const auto begin = markup(rss, "<item>", cursor);
        if (begin == missing) break;
        const auto end = markup(rss, "</item>", begin + 6);
        if (end == missing) break; // A truncated tail is expected for large feeds.
        const auto item = rss.substr(begin + 6, end - begin - 6);
        cursor = end + 7;
        const auto title_at = markup(item, "<title>");
        const auto title_end = title_at == missing ? missing : markup(item, "</title>", title_at + 7);
        const auto enclosure_at = markup(item, "<enclosure ");
        if (title_at == missing || title_end == missing || enclosure_at == missing) continue;
        const auto enclosure_end = item.find('>', enclosure_at);
        if (enclosure_end == missing) continue;
        const auto tag = item.substr(enclosure_at, enclosure_end - enclosure_at + 1);
        const auto type = attribute(tag, "type");
        const auto url = unescape(attribute(tag, "url"));
        if ((type != "audio/mpeg" && type != "audio/mp3") || !media_url(url) ||
            url.size() >= sizeof(RadioStation::url)) continue;
        bool duplicate = false;
        for (std::size_t i = 0; i < count; ++i) duplicate |= url == stations[i].url;
        if (duplicate) continue;
        RadioStation station{};
        title_copy(station.name, sizeof(station.name), unescape(item.substr(title_at + 7, title_end - title_at - 7)));
        if (!station.name[0]) continue;
        std::memcpy(station.url, url.c_str(), url.size() + 1);
        std::snprintf(station.description, sizeof(station.description), "故事FM · 在线听书");
        station.on_demand = true;
        stations[count++] = station;
    }
    return count;
}

std::size_t radio_media_parse_feed(std::string_view rss, RadioStation *stations,
                                   std::size_t capacity) {
    return parse_items(rss, stations, capacity, 0);
}

std::size_t radio_media_complete_prefix(std::string_view rss) {
    std::size_t consumed = 0;
    while (true) {
        const auto begin = markup(rss, "<item>", consumed);
        if (begin == missing) return consumed;
        const auto end = markup(rss, "</item>", begin + 6);
        if (end == missing) return consumed;
        consumed = end + 7;
    }
}

uint64_t radio_media_resume_start(uint64_t consumed) {
    return consumed > 4096 ? consumed - 4096 : 0;
}

bool radio_media_range_matches(const char *header, uint64_t expected) {
    if (!header || std::strncmp(header, "bytes ", 6) != 0) return false;
    const char *at = header + 6;
    auto number = [&at](uint64_t &value) {
        value = 0;
        if (*at < '0' || *at > '9') return false;
        while (*at >= '0' && *at <= '9') {
            const unsigned digit = static_cast<unsigned>(*at++ - '0');
            if (value > (UINT64_MAX - digit) / 10) return false;
            value = value * 10 + digit;
        }
        return true;
    };
    uint64_t start, end, total;
    if (!number(start) || *at++ != '-' || !number(end) || *at++ != '/' ||
        start != expected || end < start) return false;
    if (*at == '*') return *++at == '\0';
    return number(total) && total > end && *at == '\0';
}

bool radio_media_feed_chunk(const char *block, std::size_t size, void *context) {
    if (!context) return false;
    auto &feed = *static_cast<RadioFeedReader *>(context);
    constexpr std::size_t limit = 6144;
    if (feed.invalid || !feed.stations || !feed.capacity || !block || size > 512 ||
        feed.count >= std::min<std::size_t>(feed.capacity, 8)) return false;
    if (!feed.pending) feed.pending = static_cast<char *>(std::malloc(limit));
    if (!feed.pending || feed.used + size > limit) {
        feed.invalid = true;
        return false;
    }
    std::memcpy(feed.pending + feed.used, block, size);
    feed.used += size;
    const std::string_view xml(feed.pending, feed.used);
    if (xml.find("<!DOCTYPE") != std::string_view::npos ||
        xml.find("<!ENTITY") != std::string_view::npos) {
        feed.invalid = true;
        return false;
    }
    const auto consumed = radio_media_complete_prefix(xml);
    if (consumed) {
        feed.count = parse_items(xml.substr(0, consumed), feed.stations,
                                  std::min<std::size_t>(feed.capacity, 8), feed.count);
        feed.used -= consumed;
        std::memmove(feed.pending, feed.pending + consumed, feed.used);
    }
    return feed.count < std::min<std::size_t>(feed.capacity, 8);
}
