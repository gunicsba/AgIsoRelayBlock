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
  objects, all declared non-latching/momentary at the protocol level (a
  toggle + a hold-to-run variant per relay channel, plus a momentary
  buzzer function; see `handle_aux_function_event` in
  [vt_app.cpp](main/isobus/vt_app.cpp)). Builds and boots clean on the
  bench (no crash, no parse errors). Used `AuxiliaryFunctionType2`, not
  the `AuxiliaryFunctionType1` shown in every vendored library example --
  AgIsoStack++'s own parser logs Type 1 as ignored by VT version 3+
  terminals for real assignment.
- 2026-09-11: revised the AUX-N latching design after real-world tractor
  feedback -- most tractors only offer momentary push-buttons, and a
  tractor's own assignment menu generally only offers type-matched
  input/function pairs, so a function declared as the protocol's actual
  `BooleanLatchingOnOff` type risked not even being assignable to a real
  button. Both variants are now declared momentary at the protocol level;
  the toggle variant's "latch and stay" behavior is produced in firmware
  instead (edge-triggered: flips the relay on each rising edge, ignores
  release), so it still works with a genuinely momentary physical button.
- 2026-09-11: UI readability pass based on bench feedback -- Data Mask
  indicators bumped from 32x32 to 60x60 and reflowed into a 4-per-row x 2
  row grid (were "barely readable" in a single row of small boxes); label
  text on both the Data Mask and the soft keys bumped from an 8x8 font to
  32x32 (roughly 4x, was "way too small"); the latching AUX-N function's
  label gets a distinguishing `#` suffix (`"R1#"` vs. plain `"R1"` for the
  momentary variant) so the two show up distinctly in the tractor's own
  AUX-N assignment list. See
  [../docs/vt-ui-design.md](../docs/vt-ui-design.md#relay-indicator-grid-on-the-data-mask).
- 2026-09-11: fixed a real labeling bug found during AUX-N assignment
  testing: the toggle variant's `"R{n}#"` label had been left at its
  pre-readability-pass size (16x10, 8x8 font) -- every *other* label got
  bumped in that pass, this one was missed -- so it rendered as a clipped,
  unlabeled "R" in the tractor's AUX-N assignment list. All 8 toggle
  entries looked identical, which is exactly the kind of thing that leads
  to assigning the wrong one. Fixed: same `"R{n}"` text as the hold-to-run
  variant, sized to match, distinguished by an **underlined** font instead
  of a suffix character (per bench feedback: "I'd also expect R1 to R8
  there"). Buzzer's AUX-N designator also got its own dedicated `"B"`
  label instead of reusing SK9's `"Bz"`, so it can't clip either.
- 2026-09-11: fixed a real behavioral bug found while testing a channel
  with both a toggle and a momentary control assigned: the momentary one
  always won and forced the relay off, even without being pressed. Root
  cause: it mirrored its input value straight to the relay, but AUX-N
  input devices report status periodically even while idle, so its own
  "released" reports kept silently overriding whatever the toggle variant
  had set. Redefined the momentary variant as an override instead of a
  competing direct setter: press saves the relay's current state and
  **inverts** it, release restores the saved state -- so a latched-ON
  channel goes OFF while held and back ON on release. Also: SK1-SK8 now show
  `"R1"`-`"R8"` (underlined, matching the AUX-N toggle variant's
  convention) instead of a bare digit; buzzer labels (SK9 and the AUX-N
  function) both now say `"BZ"`; and the buzzer itself turned out to be a
  passive piezo needing a driven tone, not a static DC level -- rewrote
  [buzzer_driver.cpp](main/io/buzzer_driver.cpp) to drive it via LEDC PWM
  (~2.7kHz) instead of a plain GPIO pulse. See
  [../docs/vt-ui-design.md](../docs/vt-ui-design.md#aux-n-functions-17-total).
- 2026-09-11: added a second Soft Key Mask page (SK1-SK8 momentary-override
  keys + a back key), reached from page 1 via a new SK10 ("`>>`") using the
  VT's "Change Soft Key Mask" command -- the Data Mask itself never
  changes, just which SKM is shown alongside it. Page 2's momentary keys
  share the exact same per-channel override state as the AUX-N momentary
  function, so pressing either one for a channel doesn't leave the other
  out of sync. See
  [../docs/vt-ui-design.md](../docs/vt-ui-design.md#soft-key-masks-two-pages-reached-via-a-nextback-key).
- 2026-09-11: made this device's ISOBUS address stable across reboots (was
  picking whatever was free each time; now requests the same preferred
  address derived from the chip's MAC -- confirmed identical, 200, across
  two consecutive reboots on the bench) as a plausible partial mitigation
  for the "must restart the VT to reappear" issue. Investigating further
  turned up a likely real gap in AgIsoStack++'s own vendored
  `VirtualTerminalServer` reference implementation: reading
  `isobus_virtual_terminal_server.cpp`, `managedWorkingSetList` is only
  ever matched by comparing the actual `ControlFunction` C++ object
  reference, with no timeout-based cleanup anywhere in that file -- so if
  the server's own network manager ever loses and re-detects a client
  (creating a new CF object), the cached entry goes stale with no
  recovery path short of restarting the whole VT app. See
  [../docs/roadmap.md](../docs/roadmap.md#phase-3--minimal-vt-presence).
- 2026-09-11: fixed a real logic bug in the new SKM page-2 back key: its
  event guard used `objectID != kSoftkeyBack && keyEvent != Release` to
  decide whether to return early, which (De Morgan's) actually let the
  back key through on *every* event type instead of release only, sending
  the mask-switch command 2-3 times per press. Dropped the exemption --
  the back key needs no different treatment than SK9/SK10. Bench-confirmed
  after the fix: both page-switch directions now send exactly once and
  the underlying switch applies immediately, though the soft key *labels*
  sometimes don't visually redraw until another key is pressed -- reads as
  a VT-side repaint timing quirk, not something firmware controls (the
  switch is confirmed sent and the new page's keys already respond
  correctly right away). Also added a `VTChangeSoftKeyMaskEvent` listener
  purely for diagnostic logging.

  Visual confirmation from the bench: the Data Mask's 2x4 indicator grid
  and both SKM pages render correctly --
  ![main screen: 2x4 relay indicator grid and SKM page 1 (R1-R8 underlined, BZ, >>)](../images/main%20screen.png)
  -- and all 17 AUX-N functions are recognized by the VT, with the
  underline convention correctly distinguishing the toggle variant from
  the plain momentary one --
  ![AUX-N assignment list showing R7/R8 underlined (toggle) and R1/R2 plain (momentary), "17 function(s), 20 input(s) available"](../images/aux%20assignment.png).

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
