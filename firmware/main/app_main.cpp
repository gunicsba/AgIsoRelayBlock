// Phase 1 hardware bring-up (see docs/roadmap.md#phase-1--hardware-bring-up):
// blink the RGB status LED, toggle relay channel 1 via the TCA9554PWR I2C
// expander, read digital input 1, and run a CAN/TWAI self-test loopback.
// Phase 2 (docs/roadmap.md#phase-2--bus-presence) then hands the same TWAI
// peripheral to the real ISOBUS stack and starts NAME/address claiming.
// Phase 3 (docs/roadmap.md#phase-3--minimal-vt-presence) uploads the VT
// object pool and wires SK1-SK9 to the relays/buzzer.

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "io/buzzer_driver.hpp"
#include "io/can_selftest.hpp"
#include "io/i2c_scan.hpp"
#include "io/input_driver.hpp"
#include "io/relay_driver.hpp"
#include "io/status_led.hpp"
#include "isobus/ecu_identity.hpp"
#include "isobus/vt_app.hpp"

namespace {
constexpr const char* kTag = "app_main";
}

extern "C" void app_main(void) {
    io::status_led::init();

    // Bring-up-only LED color sanity check: logged so the color actually
    // commanded can be matched against what's visually observed, in case
    // this LED's wire order doesn't match the LED_PIXEL_FORMAT_GRB
    // assumption in status_led.cpp.
    ESP_LOGI(kTag, "LED color check: RED");
    io::status_led::set_rgb(40, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(800));
    ESP_LOGI(kTag, "LED color check: GREEN");
    io::status_led::set_rgb(0, 40, 0);
    vTaskDelay(pdMS_TO_TICKS(800));
    ESP_LOGI(kTag, "LED color check: BLUE");
    io::status_led::set_rgb(0, 0, 40);
    vTaskDelay(pdMS_TO_TICKS(800));

    io::i2c_scan::run();

    bool relay_ok = io::relay_driver::init();
    io::input_driver::init();
    io::buzzer_driver::init();
    bool can_ok = io::can_selftest::run();

    ESP_LOGI(kTag, "bring-up: relay_expander=%s can_selftest=%s",
             relay_ok ? "OK" : "FAIL", can_ok ? "PASS" : "FAIL");

    // io::relay_driver::init() already forced relay 1 (and all others) off
    // as its safe-default step (N4). Deliberately not toggling it here in a
    // loop -- a bring-up check needs to prove the I2C write path works
    // once, not click a relay forever every time this firmware boots.

    // io::can_selftest::run() above already released the TWAI peripheral
    // (twai_driver_uninstall), so it's free for the real stack to claim.
    auto internal_ecu = iso::ecu_identity::init();
    if (internal_ecu) {
        bool claimed = false;
        for (int i = 0; i < 100 && !claimed; ++i) {  // up to ~5s
            claimed = internal_ecu->get_address_valid();
            if (!claimed) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }
        ESP_LOGI(kTag, "ISOBUS address claim: %s (address=%u)",
                 claimed ? "OK" : "still pending after 5s",
                 internal_ecu->get_address());
    }
    iso::vt_app::init(internal_ecu);

    bool last_input1 = false;
    int tick = 0;

    while (true) {
        // Heartbeat: green while the relay expander is working and we hold
        // a valid ISOBUS address, red otherwise (checked live every tick,
        // since address claims can in principle be lost/re-won later), dim
        // on odd ticks so it's visibly blinking rather than solid.
        uint8_t level = (tick % 2 == 0) ? 40 : 4;
        bool bus_ok = internal_ecu && internal_ecu->get_address_valid();
        if (relay_ok && bus_ok) {
            io::status_led::set_rgb(0, level, 0);
        } else {
            io::status_led::set_rgb(level, 0, 0);
        }

        bool input1 = io::input_driver::read(1);
        if (input1 != last_input1) {
            ESP_LOGI(kTag, "input 1 -> %s", input1 ? "HIGH" : "LOW");
            last_input1 = input1;
        }

        ++tick;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
