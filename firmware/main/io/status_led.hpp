#pragma once

#include <cstdint>

// Onboard WS2812 RGB status LED (GPIO38) -- see docs/hardware.md#gpio-mapping.
// Phase 1 scope: bring-up only (prove the RMT/LED path works). Real status
// semantics (power/CAN/VT state per requirement F19) land in Phase 8.
namespace io::status_led {

void init();
void set_rgb(uint8_t r, uint8_t g, uint8_t b);

}  // namespace io::status_led
