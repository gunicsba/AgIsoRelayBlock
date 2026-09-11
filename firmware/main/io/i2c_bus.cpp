#include "io/i2c_bus.hpp"

#include "esp_check.h"
#include "esp_log.h"

namespace io::i2c_bus {

namespace {
// Bench-confirmed 2026-09-10 on real hardware via an I2C bus scan
// (io::i2c_scan): GPIO41=SCL/GPIO42=SDA acks both onboard devices
// (0x20 TCA9554PWR, 0x51 PCF85063A RTC). This is the reverse of Waveshare's
// "Implementation Logic" diagram, which reads as GPIO41=SDA/GPIO42=SCL --
// see docs/hardware.md#gpio-mapping.
constexpr gpio_num_t kSdaPin = GPIO_NUM_42;
constexpr gpio_num_t kSclPin = GPIO_NUM_41;
constexpr i2c_port_num_t kPort = I2C_NUM_0;
constexpr const char* kTag = "i2c_bus";

i2c_master_bus_handle_t g_bus = nullptr;
}  // namespace

i2c_master_bus_handle_t handle() {
    if (g_bus != nullptr) {
        return g_bus;
    }

    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = kPort;
    bus_config.sda_io_num = kSdaPin;
    bus_config.scl_io_num = kSclPin;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&bus_config, &g_bus);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        g_bus = nullptr;
    }
    return g_bus;
}

}  // namespace io::i2c_bus
