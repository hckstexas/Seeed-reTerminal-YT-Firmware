#include "hardware/sticky_buzzer.h"

#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_config.h"

namespace {
constexpr const char *kTag = "sticky_buzzer";
constexpr ledc_mode_t kMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kTimer = LEDC_TIMER_0;
constexpr ledc_channel_t kChannel = LEDC_CHANNEL_0;
constexpr ledc_timer_bit_t kResolution = LEDC_TIMER_10_BIT;
}

bool StickyBuzzer::init()
{
    ledc_timer_config_t timer_config = {};
    timer_config.speed_mode = kMode;
    timer_config.timer_num = kTimer;
    timer_config.duty_resolution = kResolution;
    timer_config.freq_hz = 2200;
    timer_config.clk_cfg = LEDC_AUTO_CLK;
    esp_err_t err = ledc_timer_config(&timer_config);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Buzzer timer init failed: %s", esp_err_to_name(err));
        return false;
    }

    ledc_channel_config_t channel_config = {};
    channel_config.gpio_num = PIN_BUZZER;
    channel_config.speed_mode = kMode;
    channel_config.channel = kChannel;
    channel_config.intr_type = LEDC_INTR_DISABLE;
    channel_config.timer_sel = kTimer;
    channel_config.duty = 0;
    channel_config.hpoint = 0;
    err = ledc_channel_config(&channel_config);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Buzzer channel init failed: %s", esp_err_to_name(err));
        return false;
    }

    ready_ = true;
    ESP_LOGI(kTag, "Buzzer ready on GPIO%d", PIN_BUZZER);
    return true;
}

void StickyBuzzer::beep(uint32_t frequency_hz, uint32_t duration_ms)
{
    if (!ready_ || frequency_hz == 0 || duration_ms == 0) return;

    ledc_set_freq(kMode, kTimer, frequency_hz);
    ledc_set_duty(kMode, kChannel, 512);
    ledc_update_duty(kMode, kChannel);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    ledc_set_duty(kMode, kChannel, 0);
    ledc_update_duty(kMode, kChannel);
}