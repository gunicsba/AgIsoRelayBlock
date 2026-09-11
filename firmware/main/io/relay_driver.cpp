#include "io/relay_driver.hpp"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "io/i2c_bus.hpp"

namespace io::relay_driver {

namespace {
constexpr uint16_t kAddr = 0x20;
constexpr uint8_t kRegOutputPort = 0x01;
constexpr uint8_t kRegConfig = 0x03;
constexpr const char* kTag = "relay_driver";

i2c_master_dev_handle_t g_dev = nullptr;
// Shadow of the Output Port register: bit0 = EXIO1 = relay 1, etc.
// ASSUMPTION (unverified on hardware): EXIO high = relay energized. If the
// bench test shows relays are actually active-low, flip this driver's sense
// here rather than at every call site.
uint8_t g_output_state = 0x00;

bool write_reg(uint8_t reg, uint8_t value) {
    if (g_dev == nullptr) {
        return false;
    }
    uint8_t buf[2] = {reg, value};
    esp_err_t err = i2c_master_transmit(g_dev, buf, sizeof(buf), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "write reg 0x%02X failed: %s", reg, esp_err_to_name(err));
        return false;
    }
    return true;
}
}  // namespace

bool init() {
    i2c_master_bus_handle_t bus = io::i2c_bus::handle();
    if (bus == nullptr) {
        return false;
    }

    i2c_device_config_t dev_config = {};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address = kAddr;
    dev_config.scl_speed_hz = 100000;

    esp_err_t err = i2c_master_bus_add_device(bus, &dev_config, &g_dev);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
        g_dev = nullptr;
        return false;
    }

    // Drive the "all off" level into the Output Port register *before*
    // switching the Configuration register to outputs, so no pin glitches
    // through an unintended state while it flips from input to output.
    g_output_state = 0x00;
    bool ok = write_reg(kRegOutputPort, g_output_state);
    ok = write_reg(kRegConfig, 0x00) && ok;  // 0 = output, for all 8 pins
    if (!ok) {
        ESP_LOGE(kTag, "TCA9554PWR did not respond at 0x%02X -- relay board not wired/powered?", kAddr);
    }
    return ok;
}

bool set_relay(uint8_t channel, bool on) {
    if (channel < 1 || channel > 8 || g_dev == nullptr) {
        return false;
    }
    uint8_t bit = static_cast<uint8_t>(1u << (channel - 1));
    uint8_t new_state = on ? static_cast<uint8_t>(g_output_state | bit)
                            : static_cast<uint8_t>(g_output_state & ~bit);
    if (!write_reg(kRegOutputPort, new_state)) {
        return false;
    }
    g_output_state = new_state;
    return true;
}

bool get_relay(uint8_t channel) {
    if (channel < 1 || channel > 8) {
        return false;
    }
    return (g_output_state & (1u << (channel - 1))) != 0;
}

}  // namespace io::relay_driver
