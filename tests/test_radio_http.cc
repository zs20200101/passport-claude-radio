#include "radio_http.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

struct FakeHttp {
    std::vector<int> codes{200}; std::string body; std::size_t pos = 0, hop = 0;
    bool fail_open = false, fail_read = false, fail_redirect = false;
    bool cleaned = false, certificate_attached = false; int opens = 0;
    char **response = nullptr;
} fake;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c) {
    fake.certificate_attached = c->crt_bundle_attach != nullptr;
    return &fake;
}
int esp_http_client_set_header(FakeHttp *, const char *, const char *) { return 0; }
int esp_http_client_open(FakeHttp *c, int) {
    // Model the constrained heap: the body must not occupy memory during TLS.
    assert(c->response && *c->response == nullptr);
    ++c->opens; return c->fail_open ? -1 : 0;
}
int64_t esp_http_client_fetch_headers(FakeHttp *) { return 0; }
int esp_http_client_get_status_code(FakeHttp *c) { return c->codes[c->hop]; }
int esp_http_client_set_redirection(FakeHttp *c) {
    if (c->fail_redirect) return -1;
    if (c->hop + 1 < c->codes.size()) ++c->hop;
    return 0;
}
int esp_http_client_read(FakeHttp *c, char *out, int cap) {
    if (c->fail_read) return -1;
    const auto n = std::min<std::size_t>(cap, c->body.size() - c->pos);
    std::memcpy(out, c->body.data() + c->pos, n); c->pos += n;
    return static_cast<int>(n);
}
int esp_http_client_close(FakeHttp *) { return 0; }
int esp_http_client_cleanup(FakeHttp *c) { c->cleaned = true; return 0; }
static void reset(char **body) { fake = {}; fake.response = body; }
int main() {
    char *body = nullptr; std::size_t size = 99;
    reset(&body); fake.codes = {302, 307, 200}; fake.body = "<rss>ok</rss>";
    assert(radio_http_get("https://example.org/feed", body, 32, &size, true));
    assert(fake.opens == 3 && fake.certificate_attached && fake.cleaned);
    assert(size == fake.body.size() && std::string(body) == fake.body);
    std::free(body); body = nullptr;
    reset(&body); fake.fail_open = true;
    assert(!radio_http_get("https://example.org/feed", body, 32, &size, true));
    assert(body == nullptr && size == 0 && fake.cleaned);
    // A failed fetch can be retried without a retained body allocation.
    reset(&body); fake.body = "retry";
    assert(radio_http_get("https://example.org/feed", body, 32, &size, true));
    assert(std::string(body) == "retry"); std::free(body); body = nullptr;
    reset(&body); fake.codes = {302};
    assert(!radio_http_get("https://example.org/feed", body, 32, &size, true));
    assert(fake.opens == 5 && body == nullptr && fake.cleaned);
    reset(&body); fake.codes = {403};
    assert(!radio_http_get("https://example.org/feed", body, 32, &size, true));
    assert(body == nullptr && fake.cleaned);
    reset(&body); fake.fail_read = true;
    assert(!radio_http_get("https://example.org/feed", body, 32, &size, true));
    assert(body && body[0] == 0 && size == 0 && fake.cleaned);
    std::free(body); body = nullptr;
    for (bool prefix : {false, true}) {
        reset(&body); fake.body = "0123456789abcdef";
        assert(radio_http_get("https://example.org/feed", body, 8, &size, prefix) == prefix);
        assert(size == 7 && std::string(body) == "0123456" && fake.cleaned);
        std::free(body); body = nullptr;
    }
    reset(&body); fake.body = std::string(100000, 'a');
    std::size_t consumed = 0;
    auto consumer = [](const char *block, std::size_t bytes, void *context) {
        auto &total = *static_cast<std::size_t *>(context);
        assert(block && bytes <= 512); total += bytes;
        return total < 1024;
    };
    assert(radio_http_get("https://example.org/feed", body, 513, &size, true, consumer, &consumed));
    assert(consumed == 1024 && size == 1024 && body[0] == 0 && fake.cleaned);
    std::free(body); body = nullptr;
    reset(&body); fake.body = std::string(100000, 'a'); consumed = 0;
    auto continue_reading = [](const char *, std::size_t bytes, void *context) {
        *static_cast<std::size_t *>(context) += bytes; return true;
    };
    assert(!radio_http_get("https://example.org/feed", body, 513, &size, true, continue_reading, &consumed));
    assert(consumed == 65536 && size == 65536 && fake.cleaned);
    std::free(body); body = nullptr;
    reset(&body);
    assert(!radio_http_get("https://example.org/feed", body, 32, &size, true));
    assert(body && size == 0 && fake.cleaned); std::free(body);
}
