#include "hardware/sticky_sensors.h"

#include <algorithm>
#include <cmath>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_config.h"

namespace {
constexpr const char *kTag = "sticky_sensors";
constexpr uint8_t kRtcAddress = 0x51;
constexpr uint8_t kEnvironmentAddress = 0x44;
constexpr uint8_t kFuelGaugeAddress = BQ27220_I2C_ADDR;

constexpr uint8_t kSht40MeasureHighPrecision = 0xFD;
constexpr uint8_t kPcf8563TimeRegister = 0x02;
constexpr uint8_t kBqVoltageRegister = 0x08;
constexpr uint8_t kBqCurrentRegister = 0x0C;
constexpr uint8_t kBqStateOfChargeRegister = 0x2C;

uint8_t bcd_to_binary(uint8_t value)
{
    return static_cast<uint8_t>((value >> 4U) * 10U + (value & 0x0FU));
}

bool valid_bcd(uint8_t value)
{
    return (value & 0x0FU) <= 9U && ((value >> 4U) & 0x0FU) <= 9U;
}

uint8_t crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0xFF;
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80U) != 0 ? static_cast<uint8_t>((crc << 1U) ^ 0x31U)
                                     : static_cast<uint8_t>(crc << 1U);
        }
    }
    return crc;
}
}

bool StickySensors::init()
{
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_1;
    bus_config.sda_io_num = static_cast<gpio_num_t>(PIN_SENSOR_SDA);
    bus_config.scl_io_num = static_cast<gpio_num_t>(PIN_SENSOR_SCL);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = 1;

    esp_err_t err = i2c_new_master_bus(&bus_config, &bus_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Sensor I2C init failed: %s", esp_err_to_name(err));
        return false;
    }

    if (!add_device(kRtcAddress, rtc_)) {
        ESP_LOGW(kTag, "PCF8563 RTC not found at 0x%02X", kRtcAddress);
    } else {
        ESP_LOGI(kTag, "PCF8563 RTC ready at 0x%02X", kRtcAddress);
    }
    if (!add_device(kEnvironmentAddress, environment_)) {
        ESP_LOGW(kTag, "SHT40 not found at 0x%02X", kEnvironmentAddress);
    } else {
        ESP_LOGI(kTag, "SHT40 ready at 0x%02X", kEnvironmentAddress);
    }
    if (!add_device(kFuelGaugeAddress, fuel_gauge_)) {
        ESP_LOGW(kTag, "BQ27220 fuel gauge not found at 0x%02X", kFuelGaugeAddress);
    } else {
        ESP_LOGI(kTag, "BQ27220 fuel gauge ready at 0x%02X", kFuelGaugeAddress);
    }

    gpio_config_t power_inputs = {};
    power_inputs.pin_bit_mask = (1ULL << PIN_CHARGE_STATE) | (1ULL << PIN_EXTERNAL_POWER);
    power_inputs.mode = GPIO_MODE_INPUT;
    power_inputs.pull_up_en = GPIO_PULLUP_DISABLE;
    power_inputs.pull_down_en = GPIO_PULLDOWN_DISABLE;
    power_inputs.intr_type = GPIO_INTR_DISABLE;
    err = gpio_config(&power_inputs);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Power input GPIO init failed: %s", esp_err_to_name(err));
    }
    return true;
}

bool StickySensors::read_clock(StickyClockReading &reading)
{
    reading = {};
    if (rtc_ == nullptr) return false;

    uint8_t data[7] = {};
    if (!read_register(rtc_, kPcf8563TimeRegister, data, sizeof(data))) return false;

    // The PCF8563 VL bit means the oscillator may have stopped and the time
    // must not be presented as a real reading.
    if ((data[0] & 0x80U) != 0 ||
        !valid_bcd(data[0] & 0x7FU) ||
        !valid_bcd(data[1]) ||
        !valid_bcd(data[2] & 0x3FU) ||
        !valid_bcd(data[3] & 0x3FU) ||
        !valid_bcd(data[4] & 0x3FU) ||
        !valid_bcd(data[5] & 0x1FU) ||
        !valid_bcd(data[6])) {
        return false;
    }

    reading.second = bcd_to_binary(data[0] & 0x7FU);
    reading.minute = bcd_to_binary(data[1] & 0x7FU);
    reading.hour = bcd_to_binary(data[2] & 0x3FU);
    reading.day = bcd_to_binary(data[3] & 0x3FU);
    reading.month = bcd_to_binary(data[5] & 0x1FU);
    reading.year = static_cast<uint16_t>(2000U + bcd_to_binary(data[6]));
    if ((data[5] & 0x80U) != 0) reading.year = static_cast<uint16_t>(reading.year - 100U);

    reading.valid = reading.month >= 1 && reading.month <= 12 &&
                    reading.day >= 1 && reading.day <= 31 &&
                    reading.hour <= 23 && reading.minute <= 59 && reading.second <= 59;
    return reading.valid;
}

bool StickySensors::read_environment(StickyEnvironmentReading &reading)
{
    reading = {};
    if (environment_ == nullptr) return false;

    const uint8_t command = kSht40MeasureHighPrecision;
    uint8_t data[6] = {};
    if (i2c_master_transmit(environment_, &command, 1, 100) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
    if (i2c_master_receive(environment_, data, sizeof(data), 100) != ESP_OK ||
        crc8(data, 2) != data[2] ||
        crc8(data + 3, 2) != data[5]) {
        return false;
    }

    const uint16_t raw_temperature = static_cast<uint16_t>(data[0] << 8U) | data[1];
    const uint16_t raw_humidity = static_cast<uint16_t>(data[3] << 8U) | data[4];
    reading.temperature_c = -45.0F + (175.0F * static_cast<float>(raw_temperature) / 65535.0F);
    reading.humidity_percent = -6.0F + (125.0F * static_cast<float>(raw_humidity) / 65535.0F);
    reading.humidity_percent = std::clamp(reading.humidity_percent, 0.0F, 100.0F);
    reading.valid = std::isfinite(reading.temperature_c) && std::isfinite(reading.humidity_percent);
    return reading.valid;
}

bool StickySensors::read_power(StickyPowerReading &reading)
{
    reading = {};
    reading.external_power = gpio_get_level(static_cast<gpio_num_t>(PIN_EXTERNAL_POWER)) != 0;
    reading.charging = reading.external_power &&
                      gpio_get_level(static_cast<gpio_num_t>(PIN_CHARGE_STATE)) == 0;
    if (fuel_gauge_ == nullptr) return false;

    uint16_t voltage = 0;
    uint16_t current = 0;
    uint16_t state_of_charge = 0;
    if (!read_word(fuel_gauge_, kBqVoltageRegister, voltage) ||
        !read_word(fuel_gauge_, kBqCurrentRegister, current) ||
        !read_word(fuel_gauge_, kBqStateOfChargeRegister, state_of_charge)) {
        return false;
    }

    reading.voltage_mv = voltage;
    reading.current_ma = static_cast<int16_t>(current);
    reading.battery_percent = static_cast<uint8_t>(std::min<uint16_t>(state_of_charge, 100));
    reading.fuel_gauge_available = true;
    return true;
}

bool StickySensors::read_register(i2c_master_dev_handle_t device,
                                   uint8_t reg,
                                   uint8_t *data,
                                   size_t length)
{
    if (device == nullptr) return false;
    return i2c_master_transmit_receive(device, &reg, 1, data, length, 100) == ESP_OK;
}

bool StickySensors::read_word(i2c_master_dev_handle_t device, uint8_t reg, uint16_t &value)
{
    uint8_t data[2] = {};
    if (!read_register(device, reg, data, sizeof(data))) return false;
    value = static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1] << 8U);
    return true;
}

bool StickySensors::add_device(uint8_t address, i2c_master_dev_handle_t &device)
{
    if (bus_ == nullptr) return false;
    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = address;
    device_config.scl_speed_hz = 400000;
    return i2c_master_bus_add_device(bus_, &device_config, &device) == ESP_OK;
}