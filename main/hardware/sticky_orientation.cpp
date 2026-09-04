#include "hardware/sticky_orientation.h"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_config.h"

namespace {
constexpr const char *kTag = "sticky_orientation";
constexpr uint8_t kWhoAmIRegister = 0x0F;
constexpr uint8_t kExpectedWhoAmI = 0x6A;
constexpr uint8_t kControl3Register = 0x12;
constexpr uint8_t kAccelerometerControlRegister = 0x10;
constexpr uint8_t kGyroscopeControlRegister = 0x11;
constexpr uint8_t kMotionDataRegister = 0x22;
constexpr TickType_t kOrientationDebounce = pdMS_TO_TICKS(700);
constexpr int16_t kGyroscopeMovingThreshold = 1800;
constexpr int64_t kQuarterTurnMicrodegrees = 65LL * 1000LL * 1000LL;
constexpr int64_t kGyroSensitivityMicrodegreesPerSecond = 8750;

int16_t little_endian_int16(const uint8_t *bytes)
{
    return static_cast<int16_t>(
        static_cast<uint16_t>(bytes[0]) |
        (static_cast<uint16_t>(bytes[1]) << 8));
}
}

bool StickyOrientation::init(i2c_master_bus_handle_t bus)
{
    if (bus == nullptr) return false;

    for (const uint8_t address : {0x6A, 0x6B}) {
        i2c_device_config_t device_config = {};
        device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device_config.device_address = address;
        device_config.scl_speed_hz = 400000;

        const esp_err_t err = i2c_master_bus_add_device(bus, &device_config, &device_);
        if (err != ESP_OK) continue;

        uint8_t who_am_i = 0;
        if (read_register(kWhoAmIRegister, &who_am_i, 1) && who_am_i == kExpectedWhoAmI) {
            ESP_LOGI(kTag, "LSM6DS3TR-C ready at 0x%02X", address);
            break;
        }

        i2c_master_bus_rm_device(device_);
        device_ = nullptr;
    }

    if (device_ == nullptr) {
        ESP_LOGW(kTag, "LSM6DS3TR-C not found");
        return false;
    }

    if (!write_register(kControl3Register, 0x44) ||
        !write_register(kAccelerometerControlRegister, 0x20) ||
        !write_register(kGyroscopeControlRegister, 0x20)) {
        ESP_LOGW(kTag, "LSM6DS3TR-C configuration failed");
        return false;
    }

    candidate_since_ = xTaskGetTickCount();
    last_motion_sample_ = candidate_since_;
    yaw_microdegrees_ = 0;
    return true;
}

bool StickyOrientation::update(DisplayOrientation &orientation)
{
    if (device_ == nullptr || locked_) return false;

    StickyMotionSample sample;
    if (!read_motion(sample)) return false;

    const TickType_t now = xTaskGetTickCount();
    const uint32_t elapsed_ms = std::min<uint32_t>(
        pdTICKS_TO_MS(now - last_motion_sample_), 1000);
    last_motion_sample_ = now;
    if (elapsed_ms > 0 && std::abs(sample.gyro_z) > kGyroscopeMovingThreshold) {
        yaw_microdegrees_ +=
            static_cast<int64_t>(sample.gyro_z) *
            kGyroSensitivityMicrodegreesPerSecond *
            static_cast<int64_t>(elapsed_ms) / 1000;
        if (std::abs(yaw_microdegrees_) >= kQuarterTurnMicrodegrees) {
            orientation_ = orientation_ == DisplayOrientation::Landscape
                               ? DisplayOrientation::Portrait
                               : DisplayOrientation::Landscape;
            candidate_ = orientation_;
            candidate_since_ = now;
            yaw_microdegrees_ = 0;
            orientation = orientation_;
            ESP_LOGI(kTag, "orientation changed to %s",
                     orientation_ == DisplayOrientation::Portrait ? "portrait" : "landscape");
            return true;
        }
    }

    if (std::abs(sample.gyro_z) > kGyroscopeMovingThreshold) {
        candidate_since_ = now;
        return false;
    }

    const DisplayOrientation detected =
        std::abs(sample.accel_x) > std::abs(sample.accel_y)
            ? DisplayOrientation::Landscape
            : DisplayOrientation::Portrait;
    if (detected != candidate_) {
        candidate_ = detected;
        candidate_since_ = now;
        return false;
    }

    if (candidate_ != orientation_ &&
        now - candidate_since_ >= kOrientationDebounce) {
        orientation_ = candidate_;
        orientation = orientation_;
        ESP_LOGI(kTag, "orientation changed to %s",
                 orientation_ == DisplayOrientation::Portrait ? "portrait" : "landscape");
        return true;
    }

    return false;
}

bool StickyOrientation::read_sample(StickyMotionSample &sample)
{
    if (device_ == nullptr) return false;
    return read_motion(sample);
}

bool StickyOrientation::read_register(uint8_t reg, uint8_t *data, size_t length)
{
    if (device_ == nullptr) return false;
    return i2c_master_transmit_receive(device_, &reg, 1, data, length, 100) == ESP_OK;
}

bool StickyOrientation::write_register(uint8_t reg, uint8_t value)
{
    if (device_ == nullptr) return false;
    const uint8_t payload[] = {reg, value};
    return i2c_master_transmit(device_, payload, sizeof(payload), 100) == ESP_OK;
}

bool StickyOrientation::read_motion(StickyMotionSample &sample)
{
    uint8_t data[12] = {};
    if (!read_register(kMotionDataRegister, data, sizeof(data))) return false;

    sample.gyro_x = little_endian_int16(data + 0);
    sample.gyro_y = little_endian_int16(data + 2);
    sample.gyro_z = little_endian_int16(data + 4);
    sample.accel_x = little_endian_int16(data + 6);
    sample.accel_y = little_endian_int16(data + 8);
    sample.accel_z = little_endian_int16(data + 10);
    return true;
}