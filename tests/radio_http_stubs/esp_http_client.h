#pragma once
#include <cstdint>
using esp_err_t = int;
constexpr int ESP_OK = 0;
struct FakeHttp;
using esp_http_client_handle_t = FakeHttp *;
struct esp_http_client_config_t {
    const char *url; int timeout_ms; int buffer_size; int buffer_size_tx;
    const char *user_agent; bool keep_alive_enable; bool disable_auto_redirect;
    int max_redirection_count; int (*crt_bundle_attach)(void *);
};
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
int esp_http_client_set_header(esp_http_client_handle_t, const char *, const char *);
int esp_http_client_open(esp_http_client_handle_t, int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_set_redirection(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t, char *, int);
int esp_http_client_close(esp_http_client_handle_t);
int esp_http_client_cleanup(esp_http_client_handle_t);
