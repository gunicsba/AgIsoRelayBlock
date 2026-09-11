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

// Called by automation::interlock whenever a channel's paired digital
// input (already debounced) changes state. Forces the relay off if it
// just became disabled (di_active transitioning to true), and reflects
// both the DI indicator box and a "disabled" marker on the channel's own
// label on the VT screen. channel is 1-8. No-op before the VT client has
// connected (nothing to reflect yet).
void set_interlock_state(int channel, bool di_active);

}  // namespace iso::vt_app
