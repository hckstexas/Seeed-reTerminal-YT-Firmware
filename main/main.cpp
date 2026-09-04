#include <cstring>

    #include "esp_err.h"
    #include "esp_log.h"
#include "driver/gpio.h"
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "nvs_flash.h"

    #include "hardware/sticky_buttons.h"
    #include "hardware/sticky_display.h"
#include "hardware/sticky_local_peripherals.h"
    #include "hardware/sticky_orientation.h"
#include "hardware/sticky_sensor_bus.h"
    #include "hardware/sticky_touch.h"
    #include "navigation_view.h"
    #include "runtime_config.h"
    #include "youtube_stats.h"
    #include "youtube_view.h"

    namespace {
    constexpr const char *kTag = "youtube_sticky";
    constexpr TickType_t kRefreshInterval = pdMS_TO_TICKS(5 * 60 * 1000);

    void render_screen(StickyDisplay &display,
                       StickyScreen screen,
                       StickyScreen home_selection,
                       const YoutubeStats &stats,
                       const StickyLocalData &local_data,
                       bool clock_24_hour,
                       const char *status_message)
    {
      switch (screen) {
      case StickyScreen::Home:
          render_home_screen(display, home_selection);
          break;
      case StickyScreen::YouTube:
          render_youtube_screen(display, stats, status_message);
          break;
      case StickyScreen::Clock:
      case StickyScreen::Environment:
      case StickyScreen::Power:
      case StickyScreen::Motion:
          render_local_screen(display, screen, local_data, clock_24_hour);
          break;
      }
    }

    StickyScreen selection_after_move(StickyScreen selection, int direction)
    {
      int index = static_cast<int>(selection);
      if (index < static_cast<int>(StickyScreen::YouTube) ||
          index > static_cast<int>(StickyScreen::Motion)) {
          index = static_cast<int>(StickyScreen::YouTube);
      }
      index += direction;
      if (index < static_cast<int>(StickyScreen::YouTube)) {
          index = static_cast<int>(StickyScreen::Motion);
      } else if (index > static_cast<int>(StickyScreen::Motion)) {
          index = static_cast<int>(StickyScreen::YouTube);
      }
      return static_cast<StickyScreen>(index);
    }
    }

    extern "C" void app_main()
    {
      esp_err_t nvs_err = nvs_flash_init();
      if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
          nvs_flash_erase();
          nvs_err = nvs_flash_init();
      }
      if (nvs_err != ESP_OK) ESP_LOGW(kTag, "NVS init failed: %s", esp_err_to_name(nvs_err));

      const esp_err_t gpio_isr_err = gpio_install_isr_service(0);
      if (gpio_isr_err != ESP_OK && gpio_isr_err != ESP_ERR_INVALID_STATE) {
          ESP_LOGW(kTag, "GPIO ISR service unavailable: %s", esp_err_to_name(gpio_isr_err));
      }

      StickyDisplay display;
      if (!display.init()) { ESP_LOGE(kTag, "Display initialization failed"); return; }
      StickyButtons buttons;
      const bool buttons_ready = buttons.init();
      if (!buttons_ready) {
          ESP_LOGW(kTag, "Physical buttons unavailable; touch navigation remains active");
      }
      StickyTouch touch;
      touch.init();
      StickySensorBus sensor_bus;
      const bool sensor_bus_ready = sensor_bus.init();
      StickyLocalPeripherals local_peripherals;
      if (sensor_bus_ready) {
          local_peripherals.init(sensor_bus.handle());
      } else {
          ESP_LOGW(kTag, "Local sensor bus unavailable");
      }
      StickyOrientation orientation_sensor;
      const bool orientation_ready = sensor_bus_ready &&
                                     orientation_sensor.init(sensor_bus.handle());
      if (!orientation_ready) {
          ESP_LOGW(kTag, "Orientation sensor unavailable; using landscape");
      }
      DisplayOrientation display_orientation = DisplayOrientation::Landscape;
      display.set_orientation(display_orientation);

      YoutubeConfig config;
      const bool configured = load_youtube_config(config);
      if (!configured && !start_youtube_provisioning()) ESP_LOGE(kTag, "Could not start setup access point");

      YoutubeStats stats;
      StickyLocalData local_data;
      bool clock_24_hour = true;
      char error_message[96] = {};
      if (!configured) std::strncpy(error_message, "CONNECT TO STICKY-YT", sizeof(error_message) - 1);
      StickyScreen current_screen = StickyScreen::YouTube;
      StickyScreen home_selection = StickyScreen::YouTube;
      render_screen(display,
                    current_screen,
                    home_selection,
                    stats,
                    local_data,
                    clock_24_hour,
                    configured ? "CONNECTING" : error_message);
      display.refresh_full();
      TickType_t next_refresh = 0;
      TickType_t next_orientation_check = 0;
      TickType_t next_local_poll = 0;
      bool first_refresh = configured;
      bool orientation_locked = false;
      bool orientation_combo_latched = false;

      while (true) {
          bool refresh_requested = current_screen == StickyScreen::YouTube &&
                                   (first_refresh || xTaskGetTickCount() >= next_refresh);
          bool screen_changed = false;
          const bool orientation_combo_pressed =
              buttons.is_pressed(StickyButtonId::Up) &&
              buttons.is_pressed(StickyButtonId::Down);
          StickyButtonEvent button_event = buttons.poll();
          const bool orientation_combo_was_latched = orientation_combo_latched;
          if (orientation_combo_pressed) {
              if (!orientation_combo_latched) {
                  orientation_locked = !orientation_locked;
                  orientation_sensor.set_locked(orientation_locked);
                  local_peripherals.beep(orientation_locked ? 90 : 180);
                  ESP_LOGI(kTag,
                           "orientation %s",
                           orientation_locked ? "locked" : "unlocked");
                  screen_changed = true;
              }
              orientation_combo_latched = true;
              button_event = {};
          } else {
              orientation_combo_latched = false;
              if (orientation_combo_was_latched &&
                  (button_event.button == StickyButtonId::Up ||
                   button_event.button == StickyButtonId::Down)) {
                  button_event = {};
              }
          }
          if (button_event.valid()) {
               local_peripherals.beep();
              if (button_event.button == StickyButtonId::Up) {
                  if (current_screen == StickyScreen::Home) {
                      home_selection = selection_after_move(home_selection, -1);
                  } else {
                      current_screen = previous_screen(current_screen);
                  }
                  screen_changed = true;
              } else if (button_event.button == StickyButtonId::Down) {
                  if (current_screen == StickyScreen::Home) {
                      home_selection = selection_after_move(home_selection, 1);
                  } else {
                      current_screen = next_screen(current_screen);
                  }
                  screen_changed = true;
              } else if (button_event.button == StickyButtonId::Ai) {
                  if (button_event.type == StickyButtonEventType::LongPress) {
                      current_screen = StickyScreen::Home;
                      screen_changed = true;
                  } else if (current_screen == StickyScreen::Home) {
                      current_screen = home_selection;
                      screen_changed = true;
                  } else if (current_screen == StickyScreen::YouTube && configured) {
                      refresh_requested = true;
                   } else if (current_screen == StickyScreen::Clock) {
                       clock_24_hour = !clock_24_hour;
                       screen_changed = true;
                  } else {
                      current_screen = StickyScreen::Home;
                      screen_changed = true;
                  }
              }
          }

          const TouchEvent touch_event = touch.poll();
           if (touch_event.type != TouchEventType::None) {
               local_peripherals.beep(25);
           }
          if (touch_event.type == TouchEventType::SwipeUp) {
              if (current_screen == StickyScreen::Home) {
                  home_selection = selection_after_move(home_selection, -1);
              } else {
                  current_screen = previous_screen(current_screen);
              }
              screen_changed = true;
          } else if (touch_event.type == TouchEventType::SwipeDown) {
              if (current_screen == StickyScreen::Home) {
                  home_selection = selection_after_move(home_selection, 1);
              } else {
                  current_screen = next_screen(current_screen);
              }
              screen_changed = true;
          } else if (touch_event.type == TouchEventType::Tap) {
              if (current_screen == StickyScreen::YouTube && configured) {
                  refresh_requested = true;
               } else if (current_screen == StickyScreen::Clock) {
                   clock_24_hour = !clock_24_hour;
                   screen_changed = true;
              } else if (current_screen == StickyScreen::Home) {
                  current_screen = home_selection;
                  screen_changed = true;
              } else {
                  current_screen = StickyScreen::Home;
                  screen_changed = true;
              }
          }

          if (orientation_ready && xTaskGetTickCount() >= next_orientation_check) {
              DisplayOrientation detected_orientation = display_orientation;
              if (orientation_sensor.update(detected_orientation)) {
                  display_orientation = detected_orientation;
                  display.set_orientation(display_orientation);
                  screen_changed = true;
              }
              next_orientation_check = xTaskGetTickCount() + pdMS_TO_TICKS(500);
          }

          if (screen_changed) {
              render_screen(display,
                            current_screen,
                            home_selection,
                            stats,
                            local_data,
                            clock_24_hour,
                            configured ? "READY" : error_message);
              display.refresh_full();
          }

          if (xTaskGetTickCount() >= next_local_poll) {
              StickyMotionSample motion_sample;
              const bool motion_ready = orientation_ready &&
                                        orientation_sensor.read_sample(motion_sample);
              const bool local_changed = local_peripherals.poll(
                  local_data,
                  motion_ready ? &motion_sample : nullptr);
              if (local_changed && current_screen != StickyScreen::YouTube) {
                  render_screen(display,
                                current_screen,
                                home_selection,
                                stats,
                                local_data,
                                clock_24_hour,
                                configured ? "READY" : error_message);
                  display.refresh_full();
              }
              next_local_poll = xTaskGetTickCount() + pdMS_TO_TICKS(5000);
          }

          if (refresh_requested) {
              first_refresh = false;
              YoutubeStats next_stats;
              char next_error[96] = {};
              if (fetch_youtube_stats(config, next_stats, next_error, sizeof(next_error))) {
                  stats = next_stats;
                  if (current_screen == StickyScreen::YouTube) {
                      render_youtube_screen(display, stats, "READY");
                  }
              } else {
                  std::strncpy(error_message, next_error[0] == '\0' ? "FETCH FAILED" : next_error, sizeof(error_message) - 1);
                  error_message[sizeof(error_message) - 1] = '\0';
                  if (current_screen == StickyScreen::YouTube) {
                      render_youtube_screen(display, stats, error_message);
                  }
                  ESP_LOGW(kTag, "Refresh failed: %s", error_message);
              }
              if (current_screen == StickyScreen::YouTube) display.refresh_full();
              next_refresh = xTaskGetTickCount() + kRefreshInterval;
          }

          vTaskDelay(pdMS_TO_TICKS(50));
      }
    }
    