#include "io/status_led.hpp"

#include "esp_log.h"
#include "led_strip.h"

namespace io::status_led {

namespace {
constexpr gpio_num_t kLedPin = GPIO_NUM_38;
constexpr const char* kTag = "status_led";

led_strip_handle_t g_strip = nullptr;
}  // namespace

void init() {
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = kLedPin;
    strip_config.max_leds = 1;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.led_pixel_format = LED_PIXEL_FORMAT_GRB;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = 10 * 1000 * 1000;

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &g_strip);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "led_strip_new_rmt_device failed: %s", esp_err_to_name(err));
        g_strip = nullptr;
        return;
    }
    led_strip_clear(g_strip);
}

void set_rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (g_strip == nullptr) {
        return;
    }
    led_strip_set_pixel(g_strip, 0, r, g, b);
    led_strip_refresh(g_strip);
}

}  // namespace io::status_led
