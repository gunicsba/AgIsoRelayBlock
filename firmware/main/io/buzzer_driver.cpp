#include "io/buzzer_driver.hpp"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace io::buzzer_driver {

namespace {
constexpr gpio_num_t kBuzzerPin = GPIO_NUM_46;
constexpr const char* kTag = "buzzer_driver";

esp_timer_handle_t g_off_timer = nullptr;

void off_timer_cb(void*) {
    gpio_set_level(kBuzzerPin, 0);
}
}  // namespace

void init() {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << kBuzzerPin;
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
    gpio_set_level(kBuzzerPin, 0);  // safe default (N4): silent on boot

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = &off_timer_cb;
    timer_args.name = "buzzer_off";
    esp_err_t err = esp_timer_create(&timer_args, &g_off_timer);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_timer_create failed: %s", esp_err_to_name(err));
        g_off_timer = nullptr;
    }
}

void pulse(uint32_t pulse_ms) {
    if (g_off_timer == nullptr) {
        return;
    }
    // Restart-safe: stop a still-pending previous pulse's off-timer before
    // starting a new one, so back-to-back SK9 presses don't fight over it.
    esp_timer_stop(g_off_timer);  // no-op (returns an error, ignored) if not running
    gpio_set_level(kBuzzerPin, 1);
    esp_timer_start_once(g_off_timer, static_cast<uint64_t>(pulse_ms) * 1000);
}

}  // namespace io::buzzer_driver
