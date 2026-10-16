#pragma once
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include <algorithm>
#include <cstddef>
#include <cstdlib>

// Explicit open/read mode does not run perform()'s redirect loop. Audio/feed
// requests carry no credentials; follow only the client's bounded HTTP redirects.
inline bool radio_http_open(esp_http_client_handle_t client, int *status) {
    for (int redirects = 0; redirects <= 4; ++redirects) {
        if (esp_http_client_open(client, 0) != ESP_OK ||
            esp_http_client_fetch_headers(client) < 0) return false;
        const int code = esp_http_client_get_status_code(client);
        if (status) *status = code;
        if (code >= 200 && code < 300) return true;
        if (redirects == 4 || (code != 301 && code != 302 && code != 303 &&
                              code != 307 && code != 308)) return false;
        if (esp_http_client_set_redirection(client) != ESP_OK) return false;
        esp_http_client_close(client);
    }
    return false;
}

// Caller owns the returned buffer, including on read failure. Keep it null
// on entry so allocation happens after the authenticated TLS handshake.
using RadioBodyConsumer = bool (*)(const char *, std::size_t, void *);
inline bool radio_http_get(const char *url, char *&response, std::size_t capacity,
              std::size_t *response_size, bool prefix_only = false,
              RadioBodyConsumer consumer = nullptr, void *context = nullptr) {
    if (!url || response || capacity < 2) return false;
    if (response_size) *response_size = 0;

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 5000;
    config.buffer_size = 2048;
    config.buffer_size_tx = 512;
    config.user_agent = "AI-Passport-Radio/1.1.1";
    config.keep_alive_enable = false;
    config.disable_auto_redirect = false;
    config.max_redirection_count = 3;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;

    esp_http_client_set_header(client, "Accept", prefix_only ? "application/rss+xml,application/xml" : "application/json");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    bool success = false;
    std::size_t used = 0;
    do {
        int status = 0;
        if (!radio_http_open(client, &status)) {
            ESP_LOGW("radio_catalog", "GET %s returned HTTP %d", url, status);
            break;
        }

        // The TLS handshake needs transient RSA/certificate memory. Reserve
        // response buffer only after it completes on this no-PSRAM device.
        response = static_cast<char *>(std::malloc(capacity));
        if (!response) {
            ESP_LOGW("radio_catalog", "Response allocation failed after HTTP open");
            break;
        }
        if (consumer) {
            // Feed pages can exceed device RAM. Reuse a small network block;
            // the consumer retains only one bounded, incomplete XML item.
            while (used < 65536) {
                const int received = esp_http_client_read(client, response,
                    static_cast<int>(std::min<std::size_t>(capacity - 1, 65536 - used)));
                if (received < 0) break;
                if (received == 0) { success = used > 0; break; }
                used += static_cast<std::size_t>(received);
                if (!consumer(response, received, context)) { success = true; break; }
            }
            break;
        }
        while (used + 1 < capacity) {
            const int received = esp_http_client_read(
                client, response + used, static_cast<int>(capacity - used - 1));
            if (received < 0) {
                used = 0;
                break;
            }
            if (received == 0) {
                success = used > 0;
                break;
            }
            used += static_cast<std::size_t>(received);
        }
        if (used + 1 >= capacity) {
            ESP_LOGW("radio_catalog", "Response from %s exceeded %u bytes", url,
                     static_cast<unsigned>(capacity));
            success = prefix_only;
        }
    } while (false);

    if (response) response[consumer ? 0 : used] = '\0';
    if (response_size) *response_size = used;
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return success;
}
