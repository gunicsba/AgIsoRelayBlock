// Phase 1 hardware bring-up (see docs/roadmap.md#phase-1--hardware-bring-up):
// blink the RGB status LED, toggle relay channel 1 via the TCA9554PWR I2C
// expander, read digital input 1, and run a CAN/TWAI self-test loopback.
// Phase 2 (docs/roadmap.md#phase-2--bus-presence) then hands the same TWAI
// peripheral to the real ISOBUS stack and starts NAME/address claiming.
// Phase 3 (docs/roadmap.md#phase-3--minimal-vt-presence) uploads the VT
// object pool and wires SK1-SK9 to the relays/buzzer.
// Phase 7 (docs/roadmap.md#phase-7--wifi-ap--ota) brings up a SoftAP and a
// local web UI (status/relay-control page mirroring the VT, plus OTA
// firmware upload).

#include "automation/interlock.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_pthread.h"
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
#include "net/wifi_ap.hpp"
#include "net/web_server.hpp"

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
    automation::interlock::init();
    io::buzzer_driver::init();
    bool can_ok = io::can_selftest::run();

    ESP_LOGI(kTag, "bring-up: relay_expander=%s can_selftest=%s",
             relay_ok ? "OK" : "FAIL", can_ok ? "PASS" : "FAIL");

    // io::relay_driver::init() already forced relay 1 (and all others) off
    // as its safe-default step (N4). Deliberately not toggling it here in a
    // loop -- a bring-up check needs to prove the I2C write path works
    // once, not click a relay forever every time this firmware boots.

    // Independent of the ISOBUS stack below -- brought up here, after the
    // CAN self-test but before the (up to 5s) address-claim wait, so
    // there's something to connect to as early into boot as possible.
    net::wifi_ap::init();

    // AgIsoStack++'s CAN hardware interface and VT client each spawn a
    // worker std::thread with a 64 KB stack (CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT,
    // sdkconfig.defaults). That fit fine in internal SRAM on its own, but
    // once WiFi came up above (its driver/lwIP buffers are a substantial,
    // fixed internal-SRAM cost) there wasn't enough left: bench-confirmed
    // as `E (...) pthread: Failed to create task!` immediately followed by
    // an abort()/reboot loop, every single boot. This board has 8 MB of
    // PSRAM sitting mostly idle (CONFIG_SPIRAM, sdkconfig.defaults) --
    // redirect every pthread stack created from this point on (by this
    // task, which is where ecu_identity::init() below spawns AgIsoStack++'s
    // threads) to PSRAM instead of internal SRAM, since none of that stack
    // usage needs to be DMA-capable or in internal RAM specifically.
    esp_pthread_cfg_t pthread_cfg = esp_pthread_get_default_config();
    pthread_cfg.stack_alloc_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    ESP_ERROR_CHECK(esp_pthread_set_cfg(&pthread_cfg));

    // io::can_selftest::run() above already released the TWAI peripheral
    // (twai_driver_uninstall), so it's free for the real stack to claim.
    auto internal_ecu = iso::ecu_identity::init();
    bool claimed = false;
    if (internal_ecu) {
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

    // Phase 7 OTA rollback (docs/architecture.md#wifi-ap--ota-planned): a
    // freshly OTA-flashed image boots in "pending verify" (sdkconfig.defaults'
    // CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) and must prove itself before
    // being trusted for the next boot too. Successfully claiming our
    // ISOBUS address is that proof -- confirm on success; on failure,
    // proactively roll back to the previous (already-proven) image rather
    // than waiting for a crash/reset to trigger it, so a bad update fails
    // fast instead of leaving the device silently non-functional on the
    // bus for however long until the next power cycle. Both calls are
    // harmless no-ops on a normal (non-OTA-pending) boot, e.g. after a
    // factory flash over USB.
    if (claimed) {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (ESP_OK != err && ESP_ERR_NOT_SUPPORTED != err) {
            ESP_LOGW(kTag, "esp_ota_mark_app_valid_cancel_rollback: %s", esp_err_to_name(err));
        }
    } else if (internal_ecu) {
        ESP_LOGE(kTag, "Failed to claim an ISOBUS address on a freshly OTA-flashed image -- rolling back");
        esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
        // Only reached if that call wasn't applicable (e.g. this isn't an
        // OTA-pending boot at all) -- otherwise the device has already
        // rebooted into the previous image by this point.
        ESP_LOGW(kTag, "esp_ota_mark_app_invalid_rollback_and_reboot: %s (continuing without rollback)", esp_err_to_name(err));
    }

    iso::vt_app::init(internal_ecu);
    net::web_server::init();

    int tick = 0;
    bool last_vt_connected = false;
    bool last_vt_partner_claimed = false;

    // 20ms cadence so automation::interlock::update() debounces digital
    // inputs (and reacts to a limit switch) quickly -- 3 samples at 20ms
    // is a 60ms settle time, not the 600ms it'd be at the old 200ms tick.
    // Everything else here (the LED heartbeat) is throttled to its own
    // slower rate via the tick counter instead of slowing the whole loop.
    while (true) {
        automation::interlock::update();

        // AgIsoStack++'s VT client already retries its own connection
        // handshake automatically in the background (every ~5s while
        // failed, immediately once a fresh VT Status broadcast is seen) --
        // there's no separate "reconnect" call to make. This just makes
        // that retry loop visible: logged on every change, so a long gap
        // with no line here is itself the evidence that it's stuck rather
        // than actively retrying.
        bool vt_partner_claimed = iso::vt_app::is_partner_claimed();
        if (vt_partner_claimed != last_vt_partner_claimed) {
            ESP_LOGI(kTag, "VT partner address claim: %s",
                     vt_partner_claimed ? "a Virtual Terminal has claimed an address" : "no Virtual Terminal on the bus (nothing matches our NAME filter)");
            last_vt_partner_claimed = vt_partner_claimed;
        }

        bool vt_connected = iso::vt_app::is_connected();
        if (vt_connected != last_vt_connected) {
            ESP_LOGI(kTag, "VT connection: %s", vt_connected ? "CONNECTED" : "not connected (client retries automatically)");
            if (vt_connected) {
                // Fresh connection == fresh object pool upload == every
                // fill/label reset to the static pool's defaults -- push
                // our actual state (relay/DI/override-checkbox) and the
                // build version back onto the screen.
                iso::vt_app::resync_display();
            }
            last_vt_connected = vt_connected;
        }

        if (tick % 100 == 0) {  // ~2s -- see refresh_wifi_client_count()'s doc comment
            iso::vt_app::refresh_wifi_client_count();
        }

        if (tick % 10 == 0) {  // ~200ms, matching the original heartbeat rate
            // Heartbeat: green while the relay expander is working and we
            // hold a valid ISOBUS address, red otherwise (checked live,
            // since address claims can in principle be lost/re-won later),
            // dim every other blink so it's visibly blinking rather than solid.
            uint8_t level = ((tick / 10) % 2 == 0) ? 40 : 4;
            bool bus_ok = internal_ecu && internal_ecu->get_address_valid();
            if (relay_ok && bus_ok) {
                io::status_led::set_rgb(0, level, 0);
            } else {
                io::status_led::set_rgb(level, 0, 0);
            }
        }

        ++tick;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
