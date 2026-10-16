#include "radio_media.h"
#include <cassert>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

static std::string item(const std::string &title, const std::string &url,
                        const std::string &type = "audio/mpeg") {
    return "<item><title>" + title + "</title><enclosure url=\"" + url +
           "\" type=\"" + type + "\"/></item>";
}
int main(int argc, char **argv) {
    RadioStation stations[8]{};
    const std::string one = item("<![CDATA[故事：你好 & 世界]]>", "https://example.org/a.mp3?q=1&amp;x=2");
    const std::string two = item("&#x6606;&#26126; &amp; FM", "http://example.org/b.mp3");
    assert(radio_media_parse_feed("<rss>" + one + two + "<item><title>unfinished", stations, 8) == 2);
    assert(std::string(stations[0].name) == "故事：你好 & 世界");
    assert(std::string(stations[0].url) == "https://example.org/a.mp3?q=1&x=2");
    assert(std::string(stations[1].name) == "昆明 & FM");
    assert(stations[0].on_demand && stations[1].frequency_decihz == 0);
    assert(radio_media_parse_feed(one + one + two, stations, 1) == 1);
    assert(radio_media_parse_feed(one + one + two, stations, 8) == 2);
    const std::string fake = "<item><description><![CDATA[</item><enclosure url=\"http://evil/a.mp3\" type=\"audio/mpeg\"/>]]></description><title>real</title><enclosure type='audio/mpeg' url='http://example.org/real.mp3'/></item>";
    assert(radio_media_parse_feed("<!--" + one + "-->" + fake, stations, 8) == 1);
    assert(std::string(stations[0].name) == "real");
    assert(std::string(stations[0].url) == "http://example.org/real.mp3");
    const auto streamed = "<rss>" + one + fake + one + two + "<item><title>tail";
    for (std::size_t block_size : {1u, 7u, 64u, 512u}) {
        RadioStation streamed_stations[8]{};
        RadioFeedReader feed{}; feed.stations = streamed_stations; feed.capacity = 8;
        for (std::size_t at = 0; at < streamed.size(); at += block_size) {
            assert(radio_media_feed_chunk(streamed.data() + at,
                std::min(block_size, streamed.size() - at), &feed));
        }
        assert(!feed.invalid && feed.count == 3);
        assert(std::string(streamed_stations[0].name) == "故事：你好 & 世界");
        assert(std::string(streamed_stations[1].name) == "real");
        assert(std::string(streamed_stations[2].name) == "昆明 & FM");
        assert(feed.used < 6144); std::free(feed.pending);
    }
    assert(radio_media_complete_prefix(one + fake + "<item>incomplete") == one.size() + fake.size());
    for (const auto &bad : {"<!DOCTYPE rss>" + one, "<!ENTITY x 'evil'>" + one,
                            "<item>" + std::string(7000, 'x')}) {
        RadioFeedReader feed{}; feed.stations = stations; feed.capacity = 8;
        for (std::size_t at = 0; at < bad.size(); ++at)
            if (!radio_media_feed_chunk(bad.data() + at, 1, &feed)) break;
        assert(feed.invalid); std::free(feed.pending);
    }
    for (const auto &url : {"file:///a.mp3", "https:///a.mp3", "https://u:p@example.org/a.mp3", "https://example.org/a b.mp3", "https://example.org/a\\b.mp3"}) {
        assert(radio_media_parse_feed(item("bad", url), stations, 8) == 0);
    }
    assert(radio_media_parse_feed(item("bad", "https://example.org/" + std::string(256, 'a')), stations, 8) == 0);
    assert(radio_media_parse_feed(item("bad", "https://example.org/a.mp3", "audio/aac"), stations, 8) == 0);
    assert(radio_media_parse_feed("<!DOCTYPE rss>" + one, stations, 8) == 0);
    assert(radio_media_parse_feed("<!ENTITY a 'b'>" + one, stations, 8) == 0);
    assert(radio_media_parse_feed(one, nullptr, 8) == 0);
    std::string chinese;
    for (int i = 0; i < 40; ++i) chinese += "昆";
    assert(radio_media_parse_feed(item(chinese, "https://example.org/a.mp3"), stations, 8) == 1);
    assert(std::strlen(stations[0].name) == 63); // complete UTF-8 boundary
    assert(radio_media_resume_start(0) == 0);
    assert(radio_media_resume_start(4096) == 0);
    assert(radio_media_resume_start(10000) == 5904);
    assert(radio_media_range_matches("bytes 5904-9999/10000", 5904));
    assert(radio_media_range_matches("bytes 0-4095/*", 0));
    for (const auto *bad : {"", "bytes 10-20/30", "bytes -0-5/9", "bytes 0--5/9", "bytes 0-5/5", "bytes 0-5/6junk", "bytes 0-5/", "bytes 0-5/*junk", "bytes 0-18446744073709551616/99"}) {
        assert(!radio_media_range_matches(bad, 0));
    }
    assert(!radio_media_range_matches(nullptr, 0));
    if (argc > 1) {
        std::ifstream feed(argv[1], std::ios::binary);
        assert(feed);
        std::string prefix(24575, '\0');
        feed.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
        prefix.resize(static_cast<std::size_t>(feed.gcount()));
        RadioFeedReader streamed_feed{}; streamed_feed.stations = stations; streamed_feed.capacity = 8;
        for (std::size_t at = 0; at < prefix.size(); at += 512)
            if (!radio_media_feed_chunk(prefix.data() + at, std::min<std::size_t>(512, prefix.size() - at), &streamed_feed)) break;
        assert(!streamed_feed.invalid && streamed_feed.count > 0);
        std::free(streamed_feed.pending);
        const auto count = radio_media_parse_feed(prefix, stations, 8);
        assert(count > 0 && count <= 8);
        for (std::size_t i = 0; i < count; ++i) assert(stations[i].on_demand && stations[i].url[0]);
    }
}
