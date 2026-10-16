idf_component_register(
    SRCS "main.cc" "claude_protocol.cc" "claude_bridge.cc" "radio_catalog.cc" "radio_display_settings.cc"
         "radio_player.cc" "radio_media.cc" "radio_sleep_timer.cc"
         "radio_ui.cc"
         "../assets/fonts/passport_font_16.c" "../assets/fonts/passport_font_dashboard.c"
         "../assets/fonts/passport_font_14.c" "../assets/fonts/passport_font_40.c" "radio_font_title.c" "fap_screenshot.c"
    INCLUDE_DIRS "."
    REQUIRES
        bsp
        nvs_flash
        esp_http_client
        esp_wifi
        esp_netif
        esp_event
        esp_timer
        esp_lcd
        esp_partition
        esp_rom
        spi_flash
        mbedtls
        json
        esp_audio_codec
        esp_driver_usb_serial_jtag
        esp-wifi-connect
)
