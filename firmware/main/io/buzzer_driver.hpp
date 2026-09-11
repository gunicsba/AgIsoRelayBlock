#pragma once

#include <cstdint>

// Onboard buzzer (GPIO46 -- docs/hardware.md#gpio-mapping). Momentary pulse
// only: matches SK9/AUX-N buzzer function's "fires on press, no stored
// on/off state" behavior (docs/vt-ui-design.md#aux-n-functions-9-total).
namespace io::buzzer_driver {

void init();

// Drives the buzzer on for pulse_ms, then off again, without blocking the
// calling task (uses a one-shot esp_timer).
void pulse(uint32_t pulse_ms = 200);

}  // namespace io::buzzer_driver
