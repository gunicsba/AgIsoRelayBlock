#pragma once

#include <cstdint>

// Relay channels 1-8, driven through the onboard TCA9554PWR I2C GPIO
// expander (addr 0x20, EXIO1-EXIO8) -- NOT direct GPIO. See
// docs/hardware.md#gpio-mapping and docs/architecture.md for why.
namespace io::relay_driver {

// Configures all 8 expander pins as outputs and forces every relay off
// (requirement N4: safe default on boot). Returns false if the expander
// didn't respond (e.g. not wired up yet on the bench).
bool init();

// channel is 1-8 (matches the silkscreen/AUX-N numbering, R1-R8).
bool set_relay(uint8_t channel, bool on);
bool get_relay(uint8_t channel);

}  // namespace io::relay_driver
