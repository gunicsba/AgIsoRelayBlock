#pragma once

#include <cstdint>

// Digital inputs 1-8, direct GPIO through bidirectional optocoupler
// isolation -- see docs/hardware.md#gpio-mapping (GPIO4..GPIO11).
//
// Phase 1 scope: raw reads only, to prove the GPIO path works end to end.
// Debounce and passive/active mode configuration are Phase 3 work (F8).
namespace io::input_driver {

void init();

// channel is 1-8 (input 1 = GPIO4 ... input 8 = GPIO11).
bool read(uint8_t channel);

}  // namespace io::input_driver
