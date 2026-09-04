idf_component_register(
      SRCS "main.cpp"
           "navigation_view.cpp"
         "runtime_config.cpp"
           "youtube_stats.cpp"
           "youtube_view.cpp"
           "hardware/sticky_display.cpp"
           "hardware/sticky_buttons.cpp"
           "hardware/sticky_buzzer.cpp"
           "hardware/sticky_orientation.cpp"
           "hardware/sticky_sensors.cpp"
           "hardware/sticky_touch.cpp"
      INCLUDE_DIRS "."
      PRIV_REQUIRES esp_http_client esp_http_server esp_wifi esp_event esp_netif nvs_flash mbedtls json esp_driver_i2c esp_driver_gpio esp_driver_ledc button gt911 seeed_epaper
    )
    