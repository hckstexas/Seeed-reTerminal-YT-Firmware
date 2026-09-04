#include "hardware/sticky_buttons.h"

#include "button_gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "pin_config.h"

namespace {
constexpr const char *kTag = "sticky_buttons";
constexpr uint32_t kLongPressMs = 1500;
constexpr uint32_t kShortPressMs = 180;

int button_index(StickyButtonId button)
{
    return static_cast<int>(button);
}

uint8_t encode_event(StickyButtonId button, StickyButtonEventType type)
{
    return static_cast<uint8_t>((button_index(button) << 4) | static_cast<int>(type));
}

StickyButtonEvent decode_event(uint8_t encoded)
{
    if (encoded == 0) return {};
    return {
        static_cast<StickyButtonId>((encoded >> 4) & 0x0F),
        static_cast<StickyButtonEventType>(encoded & 0x0F),
    };
}
}

bool StickyButtons::init()
{
    const int pins[] = {PIN_BTN_OK, PIN_BTN_UP, PIN_BTN_DOWN};
    const char *names[] = {"AI/OK", "UP", "DOWN"};

    button_config_t button_config = {};
    button_config.long_press_time = kLongPressMs;
    button_config.short_press_time = kShortPressMs;

    for (int index = 0; index < 3; ++index) {
        button_gpio_config_t gpio_config = {};
        gpio_config.gpio_num = pins[index];
        gpio_config.active_level = 0;
        gpio_config.enable_power_save = false;
        gpio_config.disable_pull = false;

        contexts_[index] = {
            this,
            static_cast<StickyButtonId>(index),
        };

        esp_err_t err = iot_button_new_gpio_device(&button_config,
                                                   &gpio_config,
                                                   &handles_[index]);
        if (err != ESP_OK || handles_[index] == nullptr) {
            ESP_LOGE(kTag, "%s button init failed on GPIO%d: %s",
                     names[index], pins[index], esp_err_to_name(err));
            return false;
        }

        err = iot_button_register_cb(handles_[index],
                                     BUTTON_SINGLE_CLICK,
                                     nullptr,
                                     on_short_press,
                                     &contexts_[index]);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "%s short press callback failed: %s",
                     names[index], esp_err_to_name(err));
            return false;
        }

        err = iot_button_register_cb(handles_[index],
                                     BUTTON_LONG_PRESS_START,
                                     nullptr,
                                     on_long_press,
                                     &contexts_[index]);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "%s long press callback failed: %s",
                     names[index], esp_err_to_name(err));
            return false;
        }

        ESP_LOGI(kTag, "%s button ready on GPIO%d", names[index], pins[index]);
    }

    return true;
}

StickyButtonEvent StickyButtons::poll()
{
    return decode_event(pending_event_.exchange(0));
}

bool StickyButtons::is_pressed(StickyButtonId button) const
{
    const int index = button_index(button);
    return index >= 0 && index < 3 &&
           handles_[index] != nullptr &&
           iot_button_get_key_level(handles_[index]) == 0;
}

void StickyButtons::on_short_press(void *, void *user_data)
{
    const auto *context = static_cast<const CallbackContext *>(user_data);
    context->owner->publish(context->button, StickyButtonEventType::ShortPress);
}

void StickyButtons::on_long_press(void *, void *user_data)
{
    const auto *context = static_cast<const CallbackContext *>(user_data);
    context->owner->publish(context->button, StickyButtonEventType::LongPress);
}

void StickyButtons::publish(StickyButtonId button, StickyButtonEventType type)
{
    pending_event_.store(encode_event(button, type));
    ESP_LOGI(kTag, "button event: %d/%d", button_index(button), static_cast<int>(type));
}