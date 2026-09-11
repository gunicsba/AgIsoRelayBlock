#pragma once

#include <memory>

#include "isobus/isobus/can_internal_control_function.hpp"

// Minimal VT presence (docs/roadmap.md#phase-3--minimal-vt-presence): builds
// the Working Set / Data Mask / Soft Key Mask object pool from
// isobus/object_pool.iop (see firmware/tools/gen_object_pool.py and
// docs/vt-ui-design.md), uploads it to the tractor's VT, and wires SK1-SK8
// (relay toggles) + SK9 (buzzer pulse) to the actual hardware.
namespace iso::vt_app {

// internal_ecu must already have a valid (or pending) address claim -- see
// iso::ecu_identity::init(). Safe to call even if the claim hasn't finished
// yet; the VT client will connect once a Virtual Terminal partner and a
// claimed address are both available.
void init(std::shared_ptr<isobus::InternalControlFunction> internal_ecu);

}  // namespace iso::vt_app
