#include "io/can_selftest.hpp"

#include <cstring>

#include "driver/twai.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace io::can_selftest {

namespace {
constexpr gpio_num_t kTxPin = GPIO_NUM_17;
constexpr gpio_num_t kRxPin = GPIO_NUM_18;
constexpr const char* kTag = "can_selftest";
}  // namespace

bool run() {
    twai_general_config_t g_config =
        TWAI_GENERAL_CONFIG_DEFAULT(kTxPin, kRxPin, TWAI_MODE_NO_ACK);
    // ISO 11783-2 bus speed.
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "twai_driver_install failed: %s", esp_err_to_name(err));
        return false;
    }

    bool ok = false;
    err = twai_start();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "twai_start failed: %s", esp_err_to_name(err));
    } else {
        twai_message_t tx_msg = {};
        tx_msg.identifier = 0x1CECFFFE;  // arbitrary 29-bit ID, self-test only
        tx_msg.extd = 1;
        // Self Reception Request: without this, a transmitted frame is NOT
        // delivered back to this node's own RX queue (that only happens for
        // frames genuinely arriving from the bus) -- needed to loop back to
        // ourselves at all, and doubly so on a bus that already has other
        // real traffic on it to sort our frame out from.
        tx_msg.self = 1;
        tx_msg.data_length_code = 8;
        for (int i = 0; i < 8; ++i) {
            tx_msg.data[i] = static_cast<uint8_t>(0xA5 ^ i);
        }

        err = twai_transmit(&tx_msg, pdMS_TO_TICKS(100));
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "twai_transmit failed: %s", esp_err_to_name(err));
        } else {
            // The bus may carry real traffic from other nodes (this board's
            // transceiver may already be wired onto a live/shared CAN bus,
            // not an isolated bench loop) -- so don't assume the first
            // received frame is our own loopback. Drain the RX queue for a
            // bounded time, looking for our own frame among whatever else
            // shows up.
            int other_frames = 0;
            TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(300);
            while (xTaskGetTickCount() < deadline) {
                twai_message_t rx_msg = {};
                err = twai_receive(&rx_msg, pdMS_TO_TICKS(50));
                if (err != ESP_OK) {
                    continue;  // timeout on this poll, keep trying until deadline
                }
                if (rx_msg.identifier == tx_msg.identifier &&
                    rx_msg.data_length_code == tx_msg.data_length_code &&
                    std::memcmp(rx_msg.data, tx_msg.data, tx_msg.data_length_code) == 0) {
                    ok = true;
                    break;
                }
                ++other_frames;
            }
            if (!ok) {
                ESP_LOGE(kTag, "loopback frame not seen (observed %d other frame(s) on the bus)",
                          other_frames);
            } else if (other_frames > 0) {
                ESP_LOGI(kTag, "loopback frame seen; also observed %d other frame(s) -- "
                          "this bus has other traffic on it", other_frames);
            }

            twai_status_info_t status = {};
            if (twai_get_status_info(&status) == ESP_OK) {
                ESP_LOGI(kTag, "  bus status: state=%d tx_err=%lu rx_err=%lu "
                          "bus_err=%lu arb_lost=%lu tx_failed=%lu rx_missed=%lu",
                          status.state, static_cast<unsigned long>(status.tx_error_counter),
                          static_cast<unsigned long>(status.rx_error_counter),
                          static_cast<unsigned long>(status.bus_error_count),
                          static_cast<unsigned long>(status.arb_lost_count),
                          static_cast<unsigned long>(status.tx_failed_count),
                          static_cast<unsigned long>(status.rx_missed_count));
            }
        }
        twai_stop();
    }

    twai_driver_uninstall();
    ESP_LOGI(kTag, "CAN self-test: %s", ok ? "PASS" : "FAIL");
    return ok;
}

}  // namespace io::can_selftest
