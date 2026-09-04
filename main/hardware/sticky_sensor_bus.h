#pragma once

#include "driver/i2c_master.h"

class StickySensorBus {
public:
    bool init();
    i2c_master_bus_handle_t handle() const { return bus_; }
    bool available() const { return bus_ != nullptr; }

private:
    i2c_master_bus_handle_t bus_ = nullptr;
};