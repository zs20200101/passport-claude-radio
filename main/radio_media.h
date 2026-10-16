#pragma once
#include "radio_player.h"
#include <cstddef>
#include <cstdint>
#include <string_view>

// Parse only complete RSS items in a bounded prefix; no XML entities/DTDs run.
std::size_t radio_media_parse_feed(std::string_view rss, RadioStation *stations,
                                   std::size_t capacity);
// End of the last complete RSS item; preserves a partial trailing item for
// bounded streaming. CDATA/comments cannot supply fake item boundaries.
std::size_t radio_media_complete_prefix(std::string_view rss);
// Rewind a little so a fresh MP3 decoder can recover its frame reservoir.
uint64_t radio_media_resume_start(uint64_t consumed);
bool radio_media_range_matches(const char *header, uint64_t expected);

struct RadioFeedReader {
    char *pending = nullptr;
    std::size_t used = 0, count = 0, capacity = 0;
    RadioStation *stations = nullptr;
    bool invalid = false;
};
// Input block <=512 bytes. Caller frees pending after completion; the pending
// XML item is capped at6 KiB, and at most8 stations are retained.
bool radio_media_feed_chunk(const char *block, std::size_t size, void *context);
