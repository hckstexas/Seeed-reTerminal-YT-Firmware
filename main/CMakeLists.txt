idf_component_register(
      SRCS "main.cpp"
           "youtube_stats.cpp"
           "youtube_view.cpp"
           "hardware/sticky_display.cpp"
           "hardware/sticky_ai_button.cpp"
           "hardware/sticky_touch.cpp"
      INCLUDE_DIRS "."
      PRIV_REQUIRES esp_http_client esp_wifi esp_event esp_netif nvs_flash mbedtls cjson button gt911 seeed_epaper
    )
    