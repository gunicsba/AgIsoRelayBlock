#pragma once

// First (hardcoded) automation rule -- see docs/roadmap.md#phase-6--automation-rules
// and docs/vt-ui-design.md: digital input DI{n} acts as a limit-switch
// style safety interlock for relay channel {n} (same number, fixed 1:1
// pairing, not yet VT-configurable). While DI{n} is active, channel {n}
// is forced off immediately and cannot be turned back on by any control
// path (SKM, AUX-N toggle, AUX-N momentary) until DI{n} goes inactive --
// and even then it stays off; the operator must explicitly command it on
// again. A limit switch releasing shouldn't by itself resume motion
// (matches this project's safe-default philosophy, requirement N4).
namespace automation::interlock {

// Assumes io::input_driver::init() has already been called.
void init();

// Debounces the 8 digital inputs and enforces the interlock (forces a
// channel off the moment its paired DI activates, via iso::vt_app). Call
// periodically, e.g. every ~20ms from the same loop as anything else that
// needs a steady tick.
void update();

// True while channel's paired DI is active (channel cannot be turned on).
// channel is 1-8.
bool is_disabled(int channel);

}  // namespace automation::interlock
