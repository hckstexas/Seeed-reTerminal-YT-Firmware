#include "hardware/sticky_sensor_bus.h"

#include "esp_err.h"
#include "esp_log.h"
#include "pin_config.h"

namespace {
constexpr const char *kTag = "sticky_sensor_bus";
}

bool StickySensorBus::init()
{
    if (bus_ != nullptr) return true;

    i2c_master_bus_config_t config = {};
    config.i2c_port = I2C_NUM_1;
    config.sda_io_num = static_cast<gpio_num_t>(PIN_SENSOR_SDA);
    config.scl_io_num = static_cast<gpio_num_t>(PIN_SENSOR_SCL);
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = 1;

    const esp_err_t err = i2c_new_master_bus(&config, &bus_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "I2C1 init failed: %s", esp_err_to_name(err));
        bus_ = nullptr;
        return false;
    }

    ESP_LOGI(kTag, "I2C1 sensor bus ready");
    return true;
}