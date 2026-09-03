#include <cstring>

    #include "esp_err.h"
    #include "esp_log.h"
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "nvs_flash.h"

    #include "hardware/sticky_ai_button.h"
    #include "hardware/sticky_display.h"
    #include "hardware/sticky_touch.h"
    #include "runtime_config.h"
    #include "youtube_stats.h"
    #include "youtube_view.h"

    namespace {
    constexpr const char *kTag = "youtube_sticky";
    constexpr TickType_t kRefreshInterval = pdMS_TO_TICKS(5 * 60 * 1000);
    }

    extern "C" void app_main()
    {
      esp_err_t nvs_err = nvs_flash_init();
      if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
          nvs_flash_erase();
          nvs_err = nvs_flash_init();
      }
      if (nvs_err != ESP_OK) ESP_LOGW(kTag, "NVS init failed: %s", esp_err_to_name(nvs_err));

      StickyDisplay display;
      if (!display.init()) { ESP_LOGE(kTag, "Display initialization failed"); return; }
      StickyAiButton button;
      button.init();
      StickyTouch touch;
      touch.init();

      YoutubeConfig config;
      const bool configured = load_youtube_config(config);
      if (!configured && !start_youtube_provisioning()) ESP_LOGE(kTag, "Could not start setup access point");

      YoutubeStats stats;
      char error_message[96] = {};
      if (!configured) std::strncpy(error_message, "CONNECT TO STICKY-YT", sizeof(error_message) - 1);
      render_youtube_screen(display, stats, configured ? "CONNECTING" : error_message);
      display.refresh_full();
      TickType_t next_refresh = 0;
      bool first_refresh = configured;

      while (true) {
          bool refresh_requested = first_refresh || xTaskGetTickCount() >= next_refresh;
          const AiButtonEvent button_event = button.poll();
          if (button_event == AiButtonEvent::ShortPress && configured) refresh_requested = true;
          const TouchEvent touch_event = touch.poll();
          if (touch_event.type == TouchEventType::Tap && configured) refresh_requested = true;
          if (refresh_requested) {
              first_refresh = false;
              YoutubeStats next_stats;
              char next_error[96] = {};
              if (fetch_youtube_stats(config, next_stats, next_error, sizeof(next_error))) {
                  stats = next_stats;
                  render_youtube_screen(display, stats, "READY");
              } else {
                  std::strncpy(error_message, next_error[0] == '\0' ? "FETCH FAILED" : next_error, sizeof(error_message) - 1);
                  error_message[sizeof(error_message) - 1] = '\0';
                  render_youtube_screen(display, stats, error_message);
                  ESP_LOGW(kTag, "Refresh failed: %s", error_message);
              }
              display.refresh_full();
              next_refresh = xTaskGetTickCount() + kRefreshInterval;
          }
          vTaskDelay(pdMS_TO_TICKS(50));
      }
    }
    