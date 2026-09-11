#pragma once

#include <memory>

#include "isobus/isobus/can_internal_control_function.hpp"

// Phase 8 (docs/roadmap.md#phase-8--robustness--polish, requirement F17):
// broadcasts DM1 (ISO 11783-12 / J1939-73 Active Diagnostic Trouble
// Codes) for the fault conditions this firmware can actually detect.
// Thin wrapper around AgIsoStack++'s isobus::DiagnosticProtocol -- see
// diagnostics.cpp for exactly which conditions are covered and why (this
// board has no per-channel relay feedback, so "the I2C write succeeded"
// is as close to "the relay actually switched" as this firmware can get
// -- see docs/hardware.md's open questions).
namespace iso::diagnostics {

// internal_ecu must already have a valid (or pending) address claim, same
// requirement as iso::vt_app::init(). Call once, after
// iso::ecu_identity::init().
void init(std::shared_ptr<isobus::InternalControlFunction> internal_ecu);

// Edge-triggered like every other state-change call in this codebase --
// call whenever the condition's state actually changes, not on every
// tick; set_vt_connection_lost/set_relay_fault below are safe to call
// redundantly with the same value (DiagnosticProtocol itself no-ops a
// redundant activate/clear), but the callers are expected to diff first
// anyway, matching e.g. automation::interlock::update()'s pattern.

// SPN for "no Virtual Terminal connection" -- active while
// iso::vt_app::is_connected() is false and a VT has previously connected
// at least once this boot (so this doesn't fire from power-on until the
// very first connection ever completes, which isn't a fault, just normal
// startup).
void set_vt_connection_lost(bool active);

// SPN for "the last relay I2C write failed" -- active immediately on any
// io::relay_driver::set_relay() failure, cleared on the next *successful*
// write to any channel (not necessarily the same one that failed) -- a
// simple, honest signal that I2C communication to the TCA9554PWR expander
// is currently unreliable, not a claim about which specific channel is at
// fault or whether its contacts actually moved.
void set_relay_fault(bool active);

}  // namespace iso::diagnostics
