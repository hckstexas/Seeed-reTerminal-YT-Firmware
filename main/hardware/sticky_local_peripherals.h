#pragma once

#include <cstdint>

#include "driver/i2c_master.h"
#include "hardware/sticky_orientation.h"

struct StickyLocalData {
    bool rtc_valid = false;
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;

    bool environment_valid = false;
    float temperature_c = 0.0f;
    float humidity_percent = 0.0f;

    bool power_valid = false;
    uint16_t voltage_mv = 0;
    int16_t current_ma = 0;
    uint16_t state_of_charge = 0;
    bool charging = false;
    bool external_power = false;

    bool motion_valid = false;
    StickyMotionSample motion;
};

class StickyLocalPeripherals {
public:
    bool init(i2c_master_bus_handle_t bus);
    bool poll(StickyLocalData &data, const StickyMotionSample *motion_sample = nullptr);
    void beep(uint32_t duration_ms = 35);

    bool rtc_available() const { return rtc_ != nullptr; }
    bool environment_available() const { return sht40_ != nullptr; }
    bool power_available() const { return fuel_gauge_ != nullptr; }

private:
    bool add_device(uint8_t address, i2c_master_dev_handle_t &device);
    bool read_register(i2c_master_dev_handle_t device, uint8_t reg, uint8_t *data, size_t length);
    bool read_word(i2c_master_dev_handle_t device, uint8_t command, uint16_t &value);
    bool read_rtc(StickyLocalData &data);
    bool read_environment(StickyLocalData &data);
    bool read_power(StickyLocalData &data);
    bool read_battery_word(uint8_t command, uint16_t &value);

    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t rtc_ = nullptr;
    i2c_master_dev_handle_t sht40_ = nullptr;
    i2c_master_dev_handle_t fuel_gauge_ = nullptr;
    bool buzzer_ready_ = false;
};