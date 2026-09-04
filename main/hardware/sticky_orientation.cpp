#include "hardware/sticky_orientation.h"

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

int16_t little_endian_int16(const uint8_t *bytes)
{
    return static_cast<int16_t>(
        static_cast<uint16_t>(bytes[0]) |
        (static_cast<uint16_t>(bytes[1]) << 8));
}
}

bool StickyOrientation::init()
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

    for (const uint8_t address : {0x6A, 0x6B}) {
        i2c_device_config_t device_config = {};
        device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device_config.device_address = address;
        device_config.scl_speed_hz = 400000;

        err = i2c_master_bus_add_device(bus_, &device_config, &device_);
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
    return true;
}

bool StickyOrientation::update(DisplayOrientation &orientation)
{
    if (device_ == nullptr) return false;

    int16_t accel_x = 0;
    int16_t accel_y = 0;
    int16_t gyro_z = 0;
    if (!read_motion(accel_x, accel_y, gyro_z)) return false;

    if (std::abs(gyro_z) > kGyroscopeMovingThreshold) {
        candidate_since_ = xTaskGetTickCount();
        return false;
    }

    const DisplayOrientation detected =
        std::abs(accel_x) > std::abs(accel_y)
            ? DisplayOrientation::Portrait
            : DisplayOrientation::Landscape;
    if (detected != candidate_) {
        candidate_ = detected;
        candidate_since_ = xTaskGetTickCount();
        return false;
    }

    if (candidate_ != orientation_ &&
        xTaskGetTickCount() - candidate_since_ >= kOrientationDebounce) {
        orientation_ = candidate_;
        orientation = orientation_;
        ESP_LOGI(kTag, "orientation changed to %s",
                 orientation_ == DisplayOrientation::Portrait ? "portrait" : "landscape");
        return true;
    }

    return false;
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

bool StickyOrientation::read_motion(int16_t &accel_x, int16_t &accel_y, int16_t &gyro_z)
{
    uint8_t data[12] = {};
    if (!read_register(kMotionDataRegister, data, sizeof(data))) return false;

    gyro_z = little_endian_int16(data + 4);
    accel_x = little_endian_int16(data + 6);
    accel_y = little_endian_int16(data + 8);
    return true;
}