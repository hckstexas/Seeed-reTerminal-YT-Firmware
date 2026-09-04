#include "hardware/sticky_local_peripherals.h"

#include <algorithm>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_config.h"

namespace {
constexpr const char *kTag = "sticky_local";
constexpr uint8_t kPcf8563Address = 0x51;
constexpr uint8_t kSht40Address = 0x44;
constexpr uint8_t kFuelGaugeAddress = BQ27220_I2C_ADDR;
constexpr uint8_t kRtcTimeRegister = 0x02;
constexpr uint8_t kSht40HighPrecisionCommand = 0xFD;
constexpr uint8_t kFuelVoltageCommand = 0x08;
constexpr uint8_t kFuelAverageCurrentCommand = 0x0B;
constexpr uint8_t kFuelStateOfChargeCommand = 0x2C;

int bcd_to_int(uint8_t value)
{
    return static_cast<int>((value >> 4) * 10 + (value & 0x0F));
}

uint8_t crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0xFF;
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) != 0 ? static_cast<uint8_t>((crc << 1) ^ 0x31)
                                    : static_cast<uint8_t>(crc << 1);
        }
    }
    return crc;
}
}

bool StickyLocalPeripherals::init(i2c_master_bus_handle_t bus)
{
    if (bus == nullptr) return false;
    bus_ = bus;

    add_device(kPcf8563Address, rtc_);
    add_device(kSht40Address, sht40_);
    add_device(kFuelGaugeAddress, fuel_gauge_);

    gpio_config_t input_config = {};
    input_config.pin_bit_mask = (1ULL << PIN_CHARGE_STATE) | (1ULL << PIN_EXTERNAL_POWER);
    input_config.mode = GPIO_MODE_INPUT;
    input_config.pull_up_en = GPIO_PULLUP_ENABLE;
    input_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    input_config.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&input_config);

    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.timer_num = LEDC_TIMER_0;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.freq_hz = 2400;
    timer.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&timer) == ESP_OK) {
        ledc_channel_config_t channel = {};
        channel.gpio_num = PIN_BUZZER;
        channel.speed_mode = LEDC_LOW_SPEED_MODE;
        channel.channel = LEDC_CHANNEL_0;
        channel.intr_type = LEDC_INTR_DISABLE;
        channel.timer_sel = LEDC_TIMER_0;
        channel.duty = 0;
        channel.hpoint = 0;
        buzzer_ready_ = ledc_channel_config(&channel) == ESP_OK;
    }

    ESP_LOGI(kTag, "local peripherals: RTC=%s SHT40=%s fuel=%s buzzer=%s",
             rtc_ != nullptr ? "yes" : "no",
             sht40_ != nullptr ? "yes" : "no",
             fuel_gauge_ != nullptr ? "yes" : "no",
             buzzer_ready_ ? "yes" : "no");
    return rtc_ != nullptr || sht40_ != nullptr || fuel_gauge_ != nullptr || buzzer_ready_;
}

bool StickyLocalPeripherals::add_device(uint8_t address, i2c_master_dev_handle_t &device)
{
    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = 400000;
    const esp_err_t err = i2c_master_bus_add_device(bus_, &config, &device);
    if (err != ESP_OK) {
        device = nullptr;
        ESP_LOGW(kTag, "device 0x%02X unavailable: %s", address, esp_err_to_name(err));
        return false;
    }
    return true;
}

bool StickyLocalPeripherals::read_register(i2c_master_dev_handle_t device,
                                            uint8_t reg,
                                            uint8_t *data,
                                            size_t length)
{
    return device != nullptr &&
           i2c_master_transmit_receive(device, &reg, 1, data, length, 100) == ESP_OK;
}

bool StickyLocalPeripherals::read_word(i2c_master_dev_handle_t device,
                                       uint8_t command,
                                       uint16_t &value)
{
    uint8_t data[2] = {};
    if (!read_register(device, command, data, sizeof(data))) return false;
    value = static_cast<uint16_t>(data[0]) |
            (static_cast<uint16_t>(data[1]) << 8);
    return true;
}

bool StickyLocalPeripherals::read_rtc(StickyLocalData &data)
{
    uint8_t raw[7] = {};
    if (!read_register(rtc_, kRtcTimeRegister, raw, sizeof(raw))) {
        data.rtc_valid = false;
        return false;
    }

    data.minute = bcd_to_int(raw[1] & 0x7F);
    data.hour = bcd_to_int(raw[2] & 0x3F);
    data.day = bcd_to_int(raw[3] & 0x3F);
    data.month = bcd_to_int(raw[5] & 0x1F);
    data.year = 2000 + bcd_to_int(raw[6]);
    data.rtc_valid = (raw[0] & 0x80) == 0 &&
                     data.month >= 1 && data.month <= 12 &&
                     data.day >= 1 && data.day <= 31 &&
                     data.hour < 24 && data.minute < 60;
    return data.rtc_valid;
}

bool StickyLocalPeripherals::read_environment(StickyLocalData &data)
{
    if (sht40_ == nullptr) {
        data.environment_valid = false;
        return false;
    }

    const uint8_t command = kSht40HighPrecisionCommand;
    uint8_t raw[6] = {};
    if (i2c_master_transmit(sht40_, &command, 1, 100) != ESP_OK) {
        data.environment_valid = false;
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    if (i2c_master_receive(sht40_, raw, sizeof(raw), 100) != ESP_OK ||
        crc8(raw, 2) != raw[2] ||
        crc8(raw + 3, 2) != raw[5]) {
        data.environment_valid = false;
        return false;
    }

    const uint16_t raw_temperature = static_cast<uint16_t>(raw[0] << 8) | raw[1];
    const uint16_t raw_humidity = static_cast<uint16_t>(raw[3] << 8) | raw[4];
    data.temperature_c = -45.0f + 175.0f * static_cast<float>(raw_temperature) / 65535.0f;
    data.humidity_percent = std::clamp(-6.0f + 125.0f * static_cast<float>(raw_humidity) / 65535.0f,
                                       0.0f,
                                       100.0f);
    data.environment_valid = true;
    return true;
}

bool StickyLocalPeripherals::read_battery_word(uint8_t command, uint16_t &value)
{
    return read_word(fuel_gauge_, command, value);
}

bool StickyLocalPeripherals::read_power(StickyLocalData &data)
{
    uint16_t voltage = 0;
    uint16_t current_raw = 0;
    uint16_t soc = 0;
    const bool voltage_ok = read_battery_word(kFuelVoltageCommand, voltage);
    const bool current_ok = read_battery_word(kFuelAverageCurrentCommand, current_raw);
    const bool soc_ok = read_battery_word(kFuelStateOfChargeCommand, soc);
    data.voltage_mv = voltage;
    data.current_ma = static_cast<int16_t>(current_raw);
    data.state_of_charge = std::min<uint16_t>(soc, 100);
    data.charging = gpio_get_level(static_cast<gpio_num_t>(PIN_CHARGE_STATE)) == 0;
    data.external_power = gpio_get_level(static_cast<gpio_num_t>(PIN_EXTERNAL_POWER)) != 0;
    data.power_valid = voltage_ok || current_ok || soc_ok;
    return data.power_valid;
}

bool StickyLocalPeripherals::poll(StickyLocalData &data, const StickyMotionSample *motion_sample)
{
    const bool rtc_ok = read_rtc(data);
    const bool environment_ok = read_environment(data);
    const bool power_ok = read_power(data);
    if (motion_sample != nullptr) {
        data.motion = *motion_sample;
        data.motion_valid = true;
    }
    return rtc_ok || environment_ok || power_ok || data.motion_valid;
}

void StickyLocalPeripherals::beep(uint32_t duration_ms)
{
    if (!buzzer_ready_) return;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}