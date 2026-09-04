#pragma once

#include <cstdint>

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "hardware/sticky_display.h"

struct StickyMotionSample {
    int16_t accel_x = 0;
    int16_t accel_y = 0;
    int16_t accel_z = 0;
    int16_t gyro_x = 0;
    int16_t gyro_y = 0;
    int16_t gyro_z = 0;
};

class StickyOrientation {
public:
    bool init(i2c_master_bus_handle_t bus);
    bool update(DisplayOrientation &orientation);
    bool read_sample(StickyMotionSample &sample);
    bool available() const { return device_ != nullptr; }

private:
    bool read_register(uint8_t reg, uint8_t *data, size_t length);
    bool write_register(uint8_t reg, uint8_t value);
    bool read_motion(StickyMotionSample &sample);

    i2c_master_dev_handle_t device_ = nullptr;
    DisplayOrientation orientation_ = DisplayOrientation::Landscape;
    DisplayOrientation candidate_ = DisplayOrientation::Landscape;
    TickType_t candidate_since_ = 0;
};