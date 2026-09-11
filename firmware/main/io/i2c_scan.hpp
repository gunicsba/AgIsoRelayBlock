#pragma once

// Bring-up diagnostic: probes every 7-bit I2C address on the shared bus
// (TCA9554PWR relay expander + PCF85063A RTC) and logs which ones ACK.
// Not part of the application proper -- kept around as a bench tool for
// when a device isn't responding at its expected address.
namespace io::i2c_scan {

void run();

}  // namespace io::i2c_scan
