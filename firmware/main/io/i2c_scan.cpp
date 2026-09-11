#include "io/i2c_scan.hpp"

#include "esp_log.h"
#include "io/i2c_bus.hpp"

namespace io::i2c_scan {

namespace {
constexpr const char* kTag = "i2c_scan";
}

void run() {
    i2c_master_bus_handle_t bus = io::i2c_bus::handle();
    if (bus == nullptr) {
        ESP_LOGE(kTag, "no bus handle");
        return;
    }

    ESP_LOGI(kTag, "scanning 0x08..0x77...");
    int found = 0;
    for (uint16_t addr = 0x08; addr <= 0x77; ++addr) {
        esp_err_t err = i2c_master_probe(bus, addr, 50);
        if (err == ESP_OK) {
            ESP_LOGI(kTag, "  ACK at 0x%02X", addr);
            ++found;
        }
    }
    ESP_LOGI(kTag, "scan done, %d device(s) found", found);
}

}  // namespace io::i2c_scan
