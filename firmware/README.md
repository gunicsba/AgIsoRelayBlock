# Firmware

ESP-IDF project for the AgIsoRelayBlock. See [../docs/roadmap.md](../docs/roadmap.md)
for where this fits; see [../docs/architecture.md](../docs/architecture.md) for the
planned module layout this will grow into.

## Current status

Phase 1 bring-up harness (`main/app_main.cpp` + `main/io/`), Phase 2 bus
presence (`main/isobus/ecu_identity.cpp`), Phase 3 minimal VT presence, and
Phase 4 AUX-N (both in `main/isobus/vt_app.cpp` + `main/isobus/object_pool.iop`):
blinks the WS2812 status LED, toggles relay channel 1 through the
TCA9554PWR I2C expander, reads digital input 1, runs a CAN/TWAI self-test
loopback, hands the TWAI peripheral to AgIsoStack++ to claim an ISO
11783-5 NAME/address, then uploads a VT object pool (8-in-a-row relay
indicators + a 9-key Soft Key Mask + 17 Auxiliary Function Type 2 objects)
and wires SK1-SK8/AUX-N to the relays and SK9/the buzzer AUX-N function to
the buzzer. Every relay channel exposes both a latching and a momentary
AUX-N function -- see [../docs/vt-ui-design.md](../docs/vt-ui-design.md#aux-n-functions-17-total)
for why.

Bench-verified on real hardware (board on COM12):
- 2026-09-10: `relay_expander=OK`, `can_selftest=PASS`. Getting there
  required fixing two real bugs found on the bench, not just in code
  review -- see [../docs/roadmap.md](../docs/roadmap.md#phase-1--hardware-bring-up)
  and [../docs/hardware.md](../docs/hardware.md#gpio-mapping):
  - The I2C SDA/SCL pin assignment in the Waveshare diagram (and this
    repo's original docs) was reversed: it's `GPIO41`=SCL, `GPIO42`=SDA,
    not the other way around.
  - A transmitted CAN frame isn't delivered back to your own RX queue
    unless you set the Self Reception Request flag (`tx_msg.self = 1`);
    plain transmit + receive will not loop back to you even with nothing
    wrong.
- 2026-09-11: ISOBUS address claim succeeds against a real, live bus with
  other existing traffic on it (this bench setup isn't an isolated CAN
  loop) -- address 129 claimed in ~350ms, no stack warnings/errors. See
  [../docs/roadmap.md](../docs/roadmap.md#phase-2--bus-presence).
- 2026-09-11: **confirmed working end-to-end on a real VT** --
  ![object pool rendered on a real VT, R1/R2 shown ON, soft keys visible](../images/test%20iop.png)
  (on [AgIsoVirtualTerminal](https://github.com/gunicsba/AgIsoVirtualTerminal)).
  Pressing the on-screen soft keys actually toggles the relays. Getting
  there took two real fixes: AgIsoStack++'s internal threads overflowed
  ESP-IDF's default 3072-byte pthread stack while processing a real AUX-N
  message from another device on the bus (bumped to 65536 in
  `sdkconfig.defaults`); and the object pool generator emitted the Data
  Mask before the Soft Key Mask it references by ID (a forward reference
  AgIsoStack++'s own parser tolerates, but reordered anyway -- see
  [../docs/roadmap.md](../docs/roadmap.md#phase-3--minimal-vt-presence)).
  **Known issues:** the buzzer (SK9) pulse is audible but very quiet
  (buzzer type unconfirmed, not yet chased further); the implement
  sometimes disappears from the VT and needs a VT restart to reappear
  (reproduced as that VT's control function going offline -- confirmed to
  be existing behavior on the VT side, independent of our pool content).
- 2026-09-11: Phase 4 AUX-N implemented -- 17 Auxiliary Function Type 2
  objects (latching + momentary variant per relay channel, plus a
  momentary buzzer function; see `handle_aux_function_event` in
  [vt_app.cpp](main/isobus/vt_app.cpp)). Builds and boots clean on the
  bench (no crash, no parse errors). Used `AuxiliaryFunctionType2`, not
  the `AuxiliaryFunctionType1` shown in every vendored library example --
  AgIsoStack++'s own parser logs Type 1 as ignored by VT version 3+
  terminals for real assignment. End-to-end assignment (an actual
  joystick button mapped and driving a relay) still needs a retest once
  the bench VT connection is stable enough to complete a pool upload and
  reach the tractor's native AUX-N assignment menu.

AgIsoStack++ is vendored as a pinned git submodule under
[components/AgIsoStack-plus-plus/upstream](components/AgIsoStack-plus-plus/upstream)
(clone with `git submodule update --init --recursive`), wrapped in a thin
`idf_component_register`-based `CMakeLists.txt` since upstream ships a
plain-CMake project rather than a native IDF component -- see
[components/AgIsoStack-plus-plus/CMakeLists.txt](components/AgIsoStack-plus-plus/CMakeLists.txt).

## Build

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/index.html)
v5.3.x with the `esp32s3` target installed.

```sh
git submodule update --init --recursive   # pulls vendored AgIsoStack++

# from an ESP-IDF export'd shell (or the VS Code ESP-IDF extension terminal)
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

Expected serial output: an LED color-check sequence (RED/GREEN/BLUE, ~800ms
each -- sanity check for the LED's actual wire color order), an I2C bus scan
(`io::i2c_scan`, lists whatever ACKs), a bring-up summary line
(`relay_expander=OK/FAIL can_selftest=PASS/FAIL`), and an input 1 state
change log line whenever GPIO4 changes level. Relay 1 is toggled once
during `relay_driver::init()` as part of the OK/FAIL check, not repeatedly
-- it won't keep clicking on every boot. `relay_expander=FAIL` most likely
means the I2C bus isn't wired as expected (check the scan output);
`can_selftest=FAIL` most likely means no CAN transceiver/bus is present at
all (still logs how many *other* frames it saw, if any -- useful if this
board is on a live/shared bus with other real traffic, not an isolated
bench loop). After that, an `ISOBUS address claim: OK (address=N)` line
(or `still pending after 5s` if no CAN partners/transceiver respond).

## Layout

See [../docs/architecture.md#module-layout-planned](../docs/architecture.md#module-layout-planned)
for the full planned layout. `io/` (bring-up + buzzer drivers) and
`isobus/` (`ecu_identity.cpp` -- NAME/address claiming; `vt_app.cpp` --
VT client, object pool, and AUX-N event handling) exist so far;
`automation/`, `config/`, and `net/` land in later phases. AUX-N handling
lives in `vt_app.cpp` rather than a separate `auxn_app.cpp` (deviation
from the originally planned split, documented in architecture.md) since
both sides share the same `VirtualTerminalClient` event dispatcher.

`isobus/object_pool.iop` and `isobus/object_pool_ids.hpp` are generated by
[tools/gen_object_pool.py](tools/gen_object_pool.py) (`py tools/gen_object_pool.py
main/isobus/object_pool.iop main/isobus/object_pool_ids.hpp`) -- edit the
script, not the generated files, and re-run it after any change to
[docs/vt-ui-design.md](../docs/vt-ui-design.md). See the script's docstring
for how to validate a changed pool against AgIsoStack++'s own parser before
flashing.
