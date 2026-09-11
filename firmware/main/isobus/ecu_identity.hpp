#pragma once

#include <memory>

#include "isobus/isobus/can_internal_control_function.hpp"

// ISOBUS bus presence: brings up the CAN hardware interface (TWAI,
// TX=GPIO17/RX=GPIO18 -- docs/hardware.md#gpio-mapping) and starts claiming
// our ISO 11783-5 NAME. See docs/isobus-protocol.md and
// docs/roadmap.md#phase-2--bus-presence.
//
// Everything in this namespace is our own project code, not part of the
// vendored AgIsoStack++ library (that lives in the `isobus::` namespace,
// e.g. isobus::NAME, isobus::InternalControlFunction below).
namespace iso::ecu_identity {

// Starts the TWAI hardware plugin and CAN network manager, constructs this
// device's NAME (manufacturer 1407 -- AgIsoStack++'s shared non-commercial
// code, see docs/isobus-protocol.md), and begins address claiming on CAN
// channel 0. Address claiming completes asynchronously on a stack-owned
// thread. Returns nullptr if the CAN hardware interface failed to start
// (e.g. TWAI/transceiver not present).
std::shared_ptr<isobus::InternalControlFunction> init();

}  // namespace iso::ecu_identity
