#pragma once

#include "bsp_button.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t claude_bridge_init(void);
void claude_bridge_set_network(bool connected);
uint32_t claude_bridge_generation(void);
bool claude_bridge_handle_key(bsp_btn_t button, bsp_btn_ev_t event, uint32_t generation);
bool claude_bridge_is_configured(void);
// Public fixed-source radio route on the already paired companion; no token in URL.
bool claude_bridge_radio_url(char *url, size_t capacity);
esp_err_t claude_bridge_configure(const char *host, const char *token, uint16_t port);

#ifdef __cplusplus
}
#endif
