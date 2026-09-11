#pragma once

#include <cstdint>

// Digital inputs 1-8, direct GPIO through bidirectional optocoupler
// isolation -- see docs/hardware.md#gpio-mapping (GPIO4..GPIO11).
namespace io::input_driver {

void init();

// channel is 1-8 (input 1 = GPIO4 ... input 8 = GPIO11).
bool read(uint8_t channel);

// Debounced reading. Call update() periodically (e.g. every ~20ms, from
// the same loop that polls anything else) to sample all 8 inputs; a level
// only becomes the new debounced state once it reads the same way for
// kDebounceSamples consecutive update() calls, filtering out mechanical
// switch bounce and electrical noise on a raw read().
constexpr uint8_t kDebounceSamples = 3;

void update();
bool read_debounced(uint8_t channel);

}  // namespace io::input_driver
