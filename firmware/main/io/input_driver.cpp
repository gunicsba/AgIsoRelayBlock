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
    // Pull-up, not pull-down: matches Waveshare's own official demo
    // firmware (WS_DIN.cpp's DIN_Init(), from the vendor's Arduino demo
    // package) exactly -- `pinMode(DIN_PINx, INPUT_PULLUP)`. This isn't
    // just an idle-state default choice, it reflects the actual circuit:
    // each channel's opto phototransistor pulls the isolated-side GPIO
    // LOW when the field-side LED is driven (input asserted) and leaves
    // it floating otherwise, so the pull direction has to be UP for an
    // unconnected/inactive input to read a defined HIGH. A pull-down here
    // (an earlier version of this code, bench-tested against floating-
    // input noise but not against the vendor's reference) still fixed the
    // floating-noise symptom, since either direction defines an idle
    // level, but it defined the WRONG idle level for this circuit --
    // confirmed by a bench report of each channel's status LED behaving
    // backwards from expectation (brighter toward DGND, unaffected by
    // COM) after that change went in. See docs/hardware.md's DI wiring
    // open question.
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

// Active-low at the raw GPIO level (see init()'s comment: the opto pulls
// the pin LOW when the input is actually asserted) -- inverted here, at
// the one point raw electrical state becomes a logical reading, so every
// caller (automation::interlock and friends) can treat read()/
// read_debounced() returning true as "input active" without needing to
// know the polarity is flipped underneath. Matches Waveshare's own demo
// firmware, which does the same inversion in software
// (WS_DIN.h's DIN_Inverse_Enable) rather than in hardware.
bool read(uint8_t channel) {
    if (channel < 1 || channel > 8) {
        return false;
    }
    return gpio_get_level(kPins[channel - 1]) == 0;
}

namespace {
bool g_debounced_state[8] = {};
uint8_t g_match_count[8] = {};  // consecutive reads matching the raw level opposite g_debounced_state
}  // namespace

void update() {
    for (int i = 0; i < 8; ++i) {
        bool raw = gpio_get_level(kPins[i]) == 0;  // active-low -- see init()'s comment
        if (raw == g_debounced_state[i]) {
            g_match_count[i] = 0;  // still agrees with the current debounced state
            continue;
        }
        if (++g_match_count[i] >= kDebounceSamples) {
            g_debounced_state[i] = raw;
            g_match_count[i] = 0;
        }
    }
}

bool read_debounced(uint8_t channel) {
    if (channel < 1 || channel > 8) {
        return false;
    }
    return g_debounced_state[channel - 1];
}

}  // namespace io::input_driver
