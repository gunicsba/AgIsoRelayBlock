#pragma once

// One-shot CAN/TWAI bring-up check: brings the driver up in NO_ACK mode
// (TX=GPIO17, RX=GPIO18 -- docs/hardware.md#gpio-mapping) at the ISO
// 11783-2 bus speed, sends one frame, and checks it comes back on RX via
// the onboard transceiver's own loopback -- no second CAN node required.
// This only proves the TWAI controller + transceiver path is alive; it does
// NOT prove correct behavior on a live, multi-node, terminated bus.
namespace io::can_selftest {

bool run();

}  // namespace io::can_selftest
