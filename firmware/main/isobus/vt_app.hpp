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

// True once the VT client's own state machine reports StateMachineState::Connected.
// AgIsoStack++'s client already retries the full connection handshake on
// its own (every ~5s while in its Failed state, immediately once back in
// Disconnected if the partner's address is valid and a fresh VT Status
// broadcast has been seen -- see isobus_virtual_terminal_client.cpp's
// update()), so there's no separate "reconnect" call to make from here.
// This exists purely so app_main can log the connection state periodically
// and make that retry loop visible on the wire, instead of only seeing
// isolated error/success log lines.
bool is_connected();

// True once a control function matching our VT NAME filter (function code
// == Virtual Terminal) has claimed an address on the bus. False here despite
// the VT app being open means the problem is upstream of us entirely --
// either it isn't transmitting on this CAN bus at all, or its NAME's
// function code doesn't match what we filter for.
bool is_partner_claimed();

// Re-pushes everything the Data Mask displays to match our actual state:
// the build version in the title (esp_app_get_description(), ESP-IDF's
// automatic `git describe --always --dirty` -- makes "which firmware
// build is actually flashed and running" visible on the VT screen, not
// just in a serial log), every relay's fill, every DI interlock's fill +
// "!" label marker, and the Momentary Override Safety checkbox. Call once
// right after is_connected() transitions to true. Necessary, not just
// nice-to-have: a fresh connection means a fresh object pool upload,
// which resets every fill/label to the static pool's built-in defaults,
// but none of our own state (relay outputs, DI status, the override
// checkbox) resets on a reconnect -- only on an actual firmware reboot --
// so without this, a VT-side hiccup and reconnect would leave the screen
// showing all-off/unchecked while the real state underneath disagreed.
void resync_display();

}  // namespace iso::vt_app
