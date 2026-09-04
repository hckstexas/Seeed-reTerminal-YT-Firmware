#pragma once

#include <cstdint>

#include "driver/i2c_master.h"

struct StickyClockReading {
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    bool valid = false;
};

struct StickyEnvironmentReading {
    float temperature_c = 0.0F;
    float humidity_percent = 0.0F;
    bool valid = false;
};

struct StickyPowerReading {
    uint16_t voltage_mv = 0;
    int16_t current_ma = 0;
    uint8_t battery_percent = 0;
    bool fuel_gauge_available = false;
    bool external_power = false;
    bool charging = false;
};

class StickySensors {
public:
    bool init();

    i2c_master_bus_handle_t bus() const { return bus_; }

    bool read_clock(StickyClockReading &reading);
    bool read_environment(StickyEnvironmentReading &reading);
    bool read_power(StickyPowerReading &reading);

private:
    bool read_register(i2c_master_dev_handle_t device,
                       uint8_t reg,
                       uint8_t *data,
                       size_t length);
    bool read_word(i2c_master_dev_handle_t device, uint8_t reg, uint16_t &value);
    bool add_device(uint8_t address, i2c_master_dev_handle_t &device);

    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t rtc_ = nullptr;
    i2c_master_dev_handle_t environment_ = nullptr;
    i2c_master_dev_handle_t fuel_gauge_ = nullptr;
};