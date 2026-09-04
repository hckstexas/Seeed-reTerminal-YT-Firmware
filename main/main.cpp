#include <cstring>

    #include "esp_err.h"
    #include "esp_log.h"
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "nvs_flash.h"

    #include "hardware/sticky_buttons.h"
    #include "hardware/sticky_buzzer.h"
    #include "hardware/sticky_display.h"
    #include "hardware/sticky_orientation.h"
    #include "hardware/sticky_sensors.h"
    #include "hardware/sticky_touch.h"
    #include "navigation_view.h"
    #include "runtime_config.h"
    #include "youtube_stats.h"
    #include "youtube_view.h"

    namespace {
    constexpr const char *kTag = "youtube_sticky";
    constexpr TickType_t kRefreshInterval = pdMS_TO_TICKS(5 * 60 * 1000);
    constexpr TickType_t kFeatureRefreshInterval = pdMS_TO_TICKS(10 * 1000);

    struct LocalReadings {
      StickyClockReading clock;
      StickyEnvironmentReading environment;
      StickyPowerReading power;
      StickyMotionReading motion;
    };

    void render_screen(StickyDisplay &display,
                       StickyScreen screen,
                       StickyScreen home_selection,
                       const YoutubeStats &stats,
                       const char *status_message,
                       const LocalReadings &local)
    {
      switch (screen) {
      case StickyScreen::Home:
          render_home_screen(display, home_selection);
          break;
      case StickyScreen::YouTube:
          render_youtube_screen(display, stats, status_message);
          break;
      case StickyScreen::Clock:
          render_clock_screen(display, local.clock);
          break;
      case StickyScreen::Environment:
          render_environment_screen(display, local.environment);
          break;
      case StickyScreen::Power:
          render_power_screen(display, local.power);
          break;
      case StickyScreen::Motion:
          render_motion_screen(display, local.motion);
          break;
      }
    }

    void update_local_reading(StickyScreen screen,
                              StickySensors &sensors,
                              StickyOrientation &orientation_sensor,
                              LocalReadings &local)
    {
      switch (screen) {
      case StickyScreen::Clock:
          sensors.read_clock(local.clock);
          break;
      case StickyScreen::Environment:
          sensors.read_environment(local.environment);
          break;
      case StickyScreen::Power:
          sensors.read_power(local.power);
          break;
      case StickyScreen::Motion:
          orientation_sensor.read_motion(local.motion);
          break;
      case StickyScreen::Home:
      case StickyScreen::YouTube:
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

      StickyDisplay display;
      if (!display.init()) { ESP_LOGE(kTag, "Display initialization failed"); return; }
      StickyButtons buttons;
      const bool buttons_ready = buttons.init();
      if (!buttons_ready) {
          ESP_LOGW(kTag, "Physical buttons unavailable; touch navigation remains active");
      }
      StickyTouch touch;
      touch.init();
      StickySensors sensors;
      const bool sensors_ready = sensors.init();
      if (!sensors_ready) {
          ESP_LOGW(kTag, "Local sensor bus unavailable");
      }
      StickyOrientation orientation_sensor;
      const bool orientation_ready = sensors_ready && orientation_sensor.init(sensors.bus());
      if (!orientation_ready) {
          ESP_LOGW(kTag, "Orientation sensor unavailable; using landscape");
      }
      StickyBuzzer buzzer;
      if (!buzzer.init()) {
          ESP_LOGW(kTag, "Buzzer unavailable");
      }
      DisplayOrientation display_orientation = DisplayOrientation::Landscape;
      display.set_orientation(display_orientation);

      YoutubeConfig config;
      const bool configured = load_youtube_config(config);
      if (!configured && !start_youtube_provisioning()) ESP_LOGE(kTag, "Could not start setup access point");

      YoutubeStats stats;
      char error_message[96] = {};
      if (!configured) std::strncpy(error_message, "CONNECT TO STICKY-YT", sizeof(error_message) - 1);
      StickyScreen current_screen = StickyScreen::YouTube;
      StickyScreen home_selection = StickyScreen::YouTube;
      LocalReadings local;
      render_screen(display,
                    current_screen,
                    home_selection,
                    stats,
                    configured ? "CONNECTING" : error_message,
                    local);
      display.refresh_full();
      TickType_t next_refresh = 0;
      TickType_t next_orientation_check = 0;
      TickType_t next_feature_refresh = 0;
      bool first_refresh = configured;

      while (true) {
          bool refresh_requested = current_screen == StickyScreen::YouTube &&
                                   (first_refresh || xTaskGetTickCount() >= next_refresh);
          bool screen_changed = false;
          const StickyButtonEvent button_event = buttons.poll();
          if (button_event.valid()) {
              if (button_event.button == StickyButtonId::Up) {
                  buzzer.beep(2400, 55);
                  if (current_screen == StickyScreen::Home) {
                      home_selection = selection_after_move(home_selection, -1);
                  } else {
                      current_screen = previous_screen(current_screen);
                  }
                  screen_changed = true;
              } else if (button_event.button == StickyButtonId::Down) {
                  buzzer.beep(2400, 55);
                  if (current_screen == StickyScreen::Home) {
                      home_selection = selection_after_move(home_selection, 1);
                  } else {
                      current_screen = next_screen(current_screen);
                  }
                  screen_changed = true;
              } else if (button_event.button == StickyButtonId::Ai) {
                  buzzer.beep(button_event.type == StickyButtonEventType::LongPress ? 1400 : 2000,
                              button_event.type == StickyButtonEventType::LongPress ? 120 : 80);
                  if (button_event.type == StickyButtonEventType::LongPress) {
                      current_screen = StickyScreen::Home;
                      screen_changed = true;
                  } else if (current_screen == StickyScreen::Home) {
                      current_screen = home_selection;
                      screen_changed = true;
                  } else if (current_screen == StickyScreen::YouTube && configured) {
                      refresh_requested = true;
                  } else {
                      current_screen = StickyScreen::Home;
                      screen_changed = true;
                  }
              }
          }

          const TouchEvent touch_event = touch.poll();
          if (touch_event.type == TouchEventType::SwipeUp) {
              buzzer.beep(2400, 55);
              if (current_screen == StickyScreen::Home) {
                  home_selection = selection_after_move(home_selection, -1);
              } else {
                  current_screen = previous_screen(current_screen);
              }
              screen_changed = true;
          } else if (touch_event.type == TouchEventType::SwipeDown) {
              buzzer.beep(2400, 55);
              if (current_screen == StickyScreen::Home) {
                  home_selection = selection_after_move(home_selection, 1);
              } else {
                  current_screen = next_screen(current_screen);
              }
              screen_changed = true;
          } else if (touch_event.type == TouchEventType::Tap) {
              buzzer.beep(2000, 80);
              if (current_screen == StickyScreen::YouTube && configured) {
                  refresh_requested = true;
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
              update_local_reading(current_screen, sensors, orientation_sensor, local);
              render_screen(display,
                            current_screen,
                            home_selection,
                            stats,
                            configured ? "READY" : error_message,
                            local);
              display.refresh_full();
              next_feature_refresh = xTaskGetTickCount() + kFeatureRefreshInterval;
          } else if (current_screen != StickyScreen::Home &&
                     current_screen != StickyScreen::YouTube &&
                     xTaskGetTickCount() >= next_feature_refresh) {
              update_local_reading(current_screen, sensors, orientation_sensor, local);
              render_screen(display,
                            current_screen,
                            home_selection,
                            stats,
                            configured ? "READY" : error_message,
                            local);
              display.refresh_partial();
              next_feature_refresh = xTaskGetTickCount() + kFeatureRefreshInterval;
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
    