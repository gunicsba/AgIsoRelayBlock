#pragma once

#include "driver/i2c_master.h"

// Shared I2C bus for the TCA9554PWR relay expander and the PCF85063A RTC
// (GPIO41=SCL, GPIO42=SDA on the Waveshare ESP32-S3-ETH-8DI-8RO-C -- see
// docs/hardware.md#gpio-mapping). Both devices live on this one bus, so all
// access goes through a single i2c_master_bus_handle_t: the IDF v5.3
// i2c_master driver serializes transactions per bus internally, which is
// what docs/architecture.md's "needs a mutex/serialized access" note
// requires -- no extra locking needed on top of it.
namespace io::i2c_bus {

// Creates the bus if it doesn't exist yet. Safe to call from multiple
// drivers' init() functions.
i2c_master_bus_handle_t handle();

}  // namespace io::i2c_bus
