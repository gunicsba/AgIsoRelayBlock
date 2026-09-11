#pragma once

#include <cstdint>

// Onboard buzzer (GPIO46 -- docs/hardware.md#gpio-mapping). It's a passive
// piezo buzzer: needs a continuously driven audio-frequency square wave to
// sound (LEDC PWM), not just a static DC level -- confirmed on the bench,
// a plain GPIO high/low pulse was barely audible. Momentary pulse only:
// matches SK9/AUX-N buzzer function's "fires on press, no stored on/off
// state" behavior (docs/vt-ui-design.md#aux-n-functions-17-total).
namespace io::buzzer_driver {

void init();

// Drives the buzzer's PWM tone for pulse_ms, then off again, without
// blocking the calling task (uses a one-shot esp_timer).
void pulse(uint32_t pulse_ms = 200);

}  // namespace io::buzzer_driver
