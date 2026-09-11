#include "io/input_driver.hpp"

#include "driver/gpio.h"

namespace io::input_driver {

namespace {
constexpr gpio_num_t kPins[8] = {
    GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6,  GPIO_NUM_7,
    GPIO_NUM_8, GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_11,
};
}  // namespace

void init() {
    uint64_t pin_mask = 0;
    for (gpio_num_t pin : kPins) {
        pin_mask |= (1ULL << pin);
    }

    gpio_config_t cfg = {};
    cfg.pin_bit_mask = pin_mask;
    cfg.mode = GPIO_MODE_INPUT;
    // The board's optocoupler input stage supplies its own bias; no
    // internal pull needed (and none assumed here pending a bench check).
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

bool read(uint8_t channel) {
    if (channel < 1 || channel > 8) {
        return false;
    }
    return gpio_get_level(kPins[channel - 1]) != 0;
}

}  // namespace io::input_driver
