#pragma once

#include <cstdint>

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "hardware/sticky_display.h"

class StickyOrientation {
public:
    bool init();
    bool update(DisplayOrientation &orientation);
    bool available() const { return device_ != nullptr; }

private:
    bool read_register(uint8_t reg, uint8_t *data, size_t length);
    bool write_register(uint8_t reg, uint8_t value);
    bool read_motion(int16_t &accel_x, int16_t &accel_y, int16_t &gyro_z);

    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t device_ = nullptr;
    DisplayOrientation orientation_ = DisplayOrientation::Landscape;
    DisplayOrientation candidate_ = DisplayOrientation::Landscape;
    TickType_t candidate_since_ = 0;
};