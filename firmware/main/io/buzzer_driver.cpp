#include "io/buzzer_driver.hpp"

#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace io::buzzer_driver {

namespace {
constexpr gpio_num_t kBuzzerPin = GPIO_NUM_46;
constexpr const char* kTag = "buzzer_driver";

// Passive piezo buzzer: needs a continuously driven square wave at an
// audible frequency to sound, not a static DC level -- a plain GPIO
// high/low pulse was barely audible on the bench. Driven via LEDC PWM.
constexpr uint32_t kToneHz = 2700;
constexpr ledc_timer_t kTimer = LEDC_TIMER_0;
constexpr ledc_channel_t kChannel = LEDC_CHANNEL_0;
constexpr ledc_mode_t kSpeedMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_bit_t kDutyResolution = LEDC_TIMER_10_BIT;  // duty range 0-1023
constexpr uint32_t kDuty50Percent = 512;

esp_timer_handle_t g_off_timer = nullptr;

void off_timer_cb(void*) {
    ledc_stop(kSpeedMode, kChannel, 0);  // 0 = idle output level (silent)
}
}  // namespace

void init() {
    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode = kSpeedMode;
    timer_cfg.duty_resolution = kDutyResolution;
    timer_cfg.timer_num = kTimer;
    timer_cfg.freq_hz = kToneHz;
    timer_cfg.clk_cfg = LEDC_AUTO_CLK;
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "ledc_timer_config failed: %s", esp_err_to_name(err));
        return;
    }

    ledc_channel_config_t channel_cfg = {};
    channel_cfg.gpio_num = kBuzzerPin;
    channel_cfg.speed_mode = kSpeedMode;
    channel_cfg.channel = kChannel;
    channel_cfg.timer_sel = kTimer;
    channel_cfg.duty = 0;  // safe default (N4): silent on boot
    channel_cfg.hpoint = 0;
    err = ledc_channel_config(&channel_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "ledc_channel_config failed: %s", esp_err_to_name(err));
        return;
    }

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = &off_timer_cb;
    timer_args.name = "buzzer_off";
    err = esp_timer_create(&timer_args, &g_off_timer);
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
    ledc_set_duty(kSpeedMode, kChannel, kDuty50Percent);
    ledc_update_duty(kSpeedMode, kChannel);
    esp_timer_start_once(g_off_timer, static_cast<uint64_t>(pulse_ms) * 1000);
}

}  // namespace io::buzzer_driver
