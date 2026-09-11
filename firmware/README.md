# Firmware

ESP-IDF project for the AgIsoRelayBlock. See [../docs/roadmap.md](../docs/roadmap.md)
for where this fits; see [../docs/architecture.md](../docs/architecture.md) for the
planned module layout this will grow into.

## Current status

Phase 1 bring-up harness (`main/app_main.cpp` + `main/io/`), Phase 2 bus
presence (`main/isobus/ecu_identity.cpp`), Phase 3 minimal VT presence,
Phase 4 AUX-N (`main/isobus/vt_app.cpp` + `main/isobus/object_pool.iop`),
and Phase 6's first automation rule (`main/automation/interlock.cpp`) --
Phase 5 (naming/icons/persistence) deliberately skipped for now, see the
roadmap. Blinks the WS2812 status LED, toggles relay channel 1 through the
TCA9554PWR I2C expander, debounces all 8 digital inputs, runs a CAN/TWAI
self-test loopback, hands the TWAI peripheral to AgIsoStack++ to claim an
ISO 11783-5 NAME/address, then uploads a VT object pool (an 8-channel
relay indicator grid with a DI state box under each one, two Soft Key Mask
pages, and 17 Auxiliary Function Type 2 objects) and wires it all up: SKM
page 1 (toggle) + page 2 (momentary override) + 17 AUX-N functions all
drive the same relays, and digital input DI{n} acts as a fixed
limit-switch interlock forcing channel {n} off. See
[../docs/vt-ui-design.md](../docs/vt-ui-design.md) for the full UI design
and why behind each of these.

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
- 2026-09-11: added Phase 6's first automation rule -- digital input DI{n}
  as a fixed limit-switch interlock for relay channel {n} (see
  [interlock.cpp](main/automation/interlock.cpp)). Skipped straight past
  the general `{input, trigger, output, action}` rule schema originally
  planned for this phase, since the concrete use case that motivated it
  (an end-of-travel switch disabling the channel that's driving toward
  it) doesn't need one yet.

  Closed out two things left open since Phase 3: added debounce to
  [input_driver.cpp](main/io/input_driver.cpp) (3 consecutive matching
  samples), and the main loop now runs at 20ms instead of 200ms so that
  settles in ~60ms rather than ~600ms (the LED heartbeat is throttled back
  to its original ~200ms blink rate independently, via the tick counter,
  so it doesn't speed up). Also added a small DI state indicator box under
  each channel on the Data Mask, and the channel's own "R{n}" label gets a
  "!" suffix while disabled -- both via the object pool's existing
  fill-attribute/string-value mechanisms, no new fonts or bitmap graphics
  needed. The interlock is enforced in exactly one place,
  `vt_app.cpp`'s `apply_relay_state` (refuses ON requests while the
  channel's DI is active), so it automatically protects every control
  path -- SKM, AUX-N toggle, AUX-N momentary -- without touching each one
  separately. See
  [../docs/vt-ui-design.md](../docs/vt-ui-design.md#digital-input-indicators--limit-switch-interlock).
- 2026-09-11: investigated "why doesn't our device reconnect on its own"
  by reading AgIsoStack++'s `isobus_virtual_terminal_client.cpp` state
  machine rather than guessing -- it already retries automatically, no
  code change needed: `Failed` resets to `Disconnected` after a fixed 5s
  timeout (visible in our logs as `"Resetting Failed VT Connection"`),
  `Disconnected` clears the last-seen VT Status timestamp so it genuinely
  waits for a fresh broadcast, and once one arrives the entire handshake
  re-runs from scratch. Added `iso::vt_app::is_connected()` and an
  edge-triggered log line in `app_main.cpp` purely for visibility, since
  the state machine's internal state isn't otherwise exposed -- makes the
  connected/not-connected transitions visible over time in future
  captures instead of inferring the retry loop from sparse log lines. See
  [../docs/roadmap.md](../docs/roadmap.md#phase-3--minimal-vt-presence).
- 2026-09-11: chased "the VT is running but the client doesn't connect"
  further. First suspected the Get Memory response from a VT log line
  reading `"Callback indicated there may be enough memory, but... it is
  impossible to be sure."` -- **ruled out**: `AgIsoVirtualTerminal`'s
  `get_is_enough_memory()` override is `return true;` unconditionally, so
  that log fires on the success path, not a rejection. Got a real answer
  from a live serial capture of our own device during a failed connection
  attempt: the handshake sails through VT Status, Working Set Master, Get
  Memory, Get Number of Softkeys, Get Text Font Data, and Get Hardware,
  then stalls on `"Get Versions Response Timeout"` every single time. That
  state is unconditional in this AgIsoStack++ version (every client passes
  through it), so any VT that doesn't answer `GetVersionsMessage` can
  never complete a connection with this client. The VT's current source
  does implement that response correctly -- the leading suspect is the
  `.exe` the user's actually running predating it (matches "the
  AgIsoVirtualTerminal I'm running is slightly different than the one we
  have locally"). Added `iso::vt_app::is_partner_claimed()` so a future
  capture can tell "no VT on the bus" apart from "VT present, handshake
  stuck" at a glance. Also confirmed two side questions from source: our
  declared VT version (hardcoded `0x06`, no public setter) only produces a
  non-fatal log warning on a version mismatch, never a rejection; and
  requesting a VT Status broadcast on demand wouldn't help either, since
  the reference server broadcasts it unconditionally every 1000ms with no
  PGN-request handling at all. Full detail in
  [../docs/roadmap.md](../docs/roadmap.md#phase-3--minimal-vt-presence).
- 2026-09-11: fixed two more real bugs found once the Phase 6 DI
  indicators were actually visible on a real VT --
  1. Two channels showed disabled/flickering with nothing wired to their
     inputs and nobody touching anything: an unconnected input genuinely
     floats and reads noise (the "no internal pull needed" comment in
     [input_driver.cpp](main/io/input_driver.cpp) had been flagged
     unverified from the start, and turned out to be wrong). Fixed by
     enabling the ESP32's internal pull-down on all 8 input pins, so an
     unconnected input settles to a defined LOW instead of floating --
     confirmed on the bench: the flickering channels went silent
     immediately. Matters more than ordinary UI noise, since these inputs
     drive a safety interlock that force-disables an output.
  2. The Data Mask's "R{n}" label box fit the plain 2-character text but
     not the disabled marker ("R1!", 3 characters). Widened the label
     (and column spacing to match) using screen space that was otherwise
     unused.
- 2026-09-11: fixed the real cause behind "Get Versions Response Timeout"
  -- corrected the previous entry here, which had called that state
  unconditional; it isn't. `isobus_virtual_terminal_client.cpp` only
  enters it when the object pool's version label is non-empty, and we'd
  set one purely so the VT could skip re-uploading an unchanged pool -- an
  optimization not worth the risk at ~1.6 KB. Dropped the version label
  entirely, so the client now skips straight to uploading instead of
  waiting on a Get Versions response with no fallback if the VT never
  answers. Also added `vt_app::send_version_info()`: pushes
  `esp_app_get_description()->version` (ESP-IDF's automatic `git describe
  --always --dirty`) to the VT's title right after connecting, so which
  exact firmware build is running is visible on the VT screen itself
  instead of only a serial log. Widened the title object's reserved
  length from 15 to 44 characters (`gen_object_pool.py`'s
  `TITLE_MAX_CHARS`) to fit. See
  [../docs/roadmap.md](../docs/roadmap.md#phase-3--minimal-vt-presence).
- 2026-09-11: dropping the version label got a real VT session all the way
  to a fully-rendered screen for the first time -- but a same-shaped issue
  then turned up separately in AUX-N joystick assignment, so this is being
  treated as a VT-side bug worth fixing properly in `AgIsoVirtualTerminal`
  (a separate session) rather than permanently working around here.
  Reverted `vt_app.cpp` back to passing the content-hash version label to
  `set_object_pool()` -- pool caching is the correct long-term behavior.
  (The build-version title text itself was confirmed showing up correctly
  on a later bench check.)
- 2026-09-11: the DI pull-down fix from earlier in this file silenced
  floating-input noise but got the polarity backwards -- caught from a
  bench report that each channel's onboard status LED behaves opposite to
  what "COM = active" would suggest. The wiki blocks automated fetches
  (403), but Waveshare's own Arduino demo package settles it directly
  (`WS_DIN.cpp`'s `DIN_Init()`): `INPUT_PULLUP` on all 8 channels, with a
  `DIN_Inverse_Enable` flag inverting the raw reading in software --
  each channel's optocoupler pulls the isolated-side GPIO LOW when the
  input is actually asserted, so the correct idle bias is pull-**up**
  with an active-**low** reading, not the pull-down from before. Switched
  [input_driver.cpp](main/io/input_driver.cpp) to `GPIO_PULLUP_ENABLE`
  and inverted the raw-to-logical reading at that same single point, so
  every caller is unaffected. Built, flashed to COM12. Bench follow-up:
  unconnected channels (DI4-DI7) now show intermittent chatter, not yet
  root-caused -- see
  [../docs/roadmap.md](../docs/roadmap.md#phase-6--automation-rules) for
  status.
- 2026-09-11: added a "Momentary Override Safety" checkbox (SKM page 2,
  SK10) letting the operator deliberately allow the momentary override
  path only (never the toggle paths) to bypass a channel's DI interlock --
  e.g. an auto-mode that normally stops a hydraulic cylinder at a soft
  limit, with an escape hatch for when it needs to go further on purpose.
  Never persisted across a reboot, same reasoning as relay outputs already
  not persisting (N4). Building it surfaced a real gap: nothing re-synced
  relay/DI/checkbox visual state after a VT *reconnect* (a fresh pool
  upload resets every fill/label to static defaults, but none of our state
  resets on a reconnect) -- fixed by replacing the narrower
  `send_version_info()` with a general `resync_display()` that pushes
  everything on every fresh connection. Also documented a real gap found
  in vendored AgIsoStack++ while looking into AUX-N assignment persistence
  -- see [Known gaps in vendored AgIsoStack++](#known-gaps-in-vendored-agisostack)
  above. See
  [../docs/vt-ui-design.md](../docs/vt-ui-design.md#momentary-override-safety-checkbox)
  and [../docs/roadmap.md](../docs/roadmap.md#phase-6--automation-rules).
- 2026-09-11: Phase 7 (WiFi AP + OTA + local web UI) -- see
  [net/wifi_ap.cpp](main/net/wifi_ap.cpp) and
  [net/web_server.cpp](main/net/web_server.cpp). Always-on SoftAP
  (`AgIsoBlock-XXXX`, random NVS-persisted password); a single-page web UI
  mirroring the VT (poll + toggle each relay, see DI interlock status) at
  `/`, plus an OTA upload form at `/ota` that streams straight into the
  inactive partition via `esp_ota_ops` (not `esp_https_ota` -- that's an
  HTTPS *client* for pulling from a remote URL, the wrong shape for a
  browser upload). Required switching to a custom dual-OTA partition table
  ([partitions.csv](partitions.csv), 2 MB slots) since the WiFi/HTTP stack
  overflowed the default table's 1 MB slots (~1.2 MB image).

  Hit a real, 100%-reproducible crash getting there: `E (...) pthread:
  Failed to create task!` immediately followed by `abort()`, every single
  boot, right after the SoftAP came up. Root cause: WiFi's own buffers
  claim a large, fixed chunk of internal SRAM, which collided with
  AgIsoStack++'s worker threads' 64 KB stacks
  (`CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT`, from the earlier Phase 2
  stack-overflow fix) -- there wasn't enough internal RAM left for both.
  Fixed by redirecting pthread stacks to PSRAM
  (`esp_pthread_set_cfg()` with `stack_alloc_caps = MALLOC_CAP_SPIRAM |
  MALLOC_CAP_8BIT`, `app_main.cpp`, right before `ecu_identity::init()`
  spawns those threads) -- this board has 8 MB of PSRAM sitting mostly
  idle, and none of that stack usage needs internal RAM specifically.
  Bench-confirmed clean boot after the fix: SoftAP up, ISOBUS address
  claimed (~360 ms), web server started, no crash.

  OTA rollback wired to the same address-claim check already used for the
  boot LED/logging: confirms the image valid
  (`esp_ota_mark_app_valid_cancel_rollback`) on success, or proactively
  rolls back (`esp_ota_mark_app_invalid_rollback_and_reboot`) on failure,
  rather than waiting for a crash to trigger the bootloader's own
  rollback. See
  [../docs/roadmap.md](../docs/roadmap.md#phase-7--wifi-ap--ota) and
  [../docs/architecture.md](../docs/architecture.md#wifi-ap--ota-planned).

AgIsoStack++ is vendored as a pinned git submodule under
[components/AgIsoStack-plus-plus/upstream](components/AgIsoStack-plus-plus/upstream)
(clone with `git submodule update --init --recursive`), wrapped in a thin
`idf_component_register`-based `CMakeLists.txt` since upstream ships a
plain-CMake project rather than a native IDF component -- see
[components/AgIsoStack-plus-plus/CMakeLists.txt](components/AgIsoStack-plus-plus/CMakeLists.txt).

## Known gaps in vendored AgIsoStack++

Bugs/gaps found in the vendored library itself while building this
project, worth fixing upstream rather than working around here. See
[docs/roadmap.md](../docs/roadmap.md#phase-4--aux-n) for the full
investigation trail behind each.

**AUX-N preferred-assignment persistence is unimplemented (2026-09-11).**
`VirtualTerminalClient` sends/receives all the right messages at all the
right protocol moments (confirmed via the `"Sent preferred assignments
after ..."` log lines in `isobus_virtual_terminal_client.cpp`), but three
`//! @todo` comments mark where the actual load-from/save-to persistent
storage should happen and never does:

- `send_auxiliary_functions_preferred_assignment()` (~line 1980) always
  sends a `PreferredAssignmentCommand` announcing zero preferred
  assignments -- `//! @todo load preferred assignment from saved
  configuration`.
- The `PreferredAssignmentCommand` response handler (~line 2624) doesn't
  load the confirmed assignment into `assignedAuxiliaryInputDevices` --
  `//! @todo load the preferred assignment into
  parentVT->assignedAuxiliaryInputDevices`.
- The `AuxiliaryAssignmentTypeTwoCommand` handler, in both the
  unassign (~line 2662) and assign (~line 2690) branches, checks
  `storeAsPreferred` and does nothing with it -- `//! @todo save preferred
  assignment to persistent configuration`.

Net effect: every reconnect requires the operator to manually reassign
every joystick/armrest button to our AUX-N functions from the tractor's
own AUX-N menu, since the client can never tell the VT what it remembers
from last time (because it remembers nothing). Ready-to-use prompt for
fixing this upstream, written to be handed to a fresh Claude Code session
with no other context, working directly in a clone of
[AgIsoStack-plus-plus](https://github.com/Open-Agriculture/AgIsoStack-plus-plus)
(this repo's `firmware/components/AgIsoStack-plus-plus/upstream` submodule
remote):

> I'm looking at `isobus_virtual_terminal_client.cpp` in this repo
> (AgIsoStack++, a C++ ISOBUS/J1939 stack). AUX-N "preferred assignment"
> support is half-implemented: the protocol messaging is all correct
> (`send_auxiliary_functions_preferred_assignment()` is called at the
> right moments, from `SendWorkingSetMasterMessage`'s state-machine
> handling after both `LoadVersionCommand` and `EndOfObjectPoolMessage`),
> but the actual persistence behind it is just three TODO comments and a
> hardcoded empty response:
>
> 1. `send_auxiliary_functions_preferred_assignment()` (around line 1980)
>    builds `{ Function::PreferredAssignmentCommand, 0 }` -- always zero
>    preferred assignments -- next to `//! @todo load preferred assignment
>    from saved configuration`.
> 2. The `Function::PreferredAssignmentCommand` response handler (around
>    line 2624) has `//! @todo load the preferred assignment into
>    parentVT->assignedAuxiliaryInputDevices` and does nothing on success.
> 3. The `Function::AuxiliaryAssignmentTypeTwoCommand` handler has two
>    `//! @todo save preferred assignment to persistent configuration`
>    spots (around lines 2662 and 2690, the unassign and assign branches)
>    gated on the incoming message's `storeAsPreferred` bit, doing
>    nothing either.
>
> `assignedAuxiliaryInputDevices` is a
> `std::vector<AssignedAuxiliaryInputDevice>` (see the struct in
> `isobus_virtual_terminal_client.hpp`, holding NAME + model
> identification code + a `std::vector<AssignedAuxiliaryFunction>` of
> function/input/type triples) -- that's the in-memory shape; nothing
> currently writes or reads it to storage.
>
> Please implement real persistence behind these three TODOs. This
> library runs on embedded targets without a filesystem as standard (this
> project's own consumer is ESP-IDF/ESP32), so don't assume one -- design
> a small abstraction (e.g. a virtual/injectable `AuxiliaryPreferredAssignmentRepository`
> interface with `load()`/`save()`, or whatever fits this codebase's
> existing patterns for injectable persistence, if it has any already --
> check `isobus_virtual_terminal_client.hpp`'s constructors and any
> similar interfaces first) so each platform's consumer can back it with
> whatever it has (NVS on ESP32, a file on Linux/PC targets, etc.),
> defaulting to a no-op/in-memory implementation so behavior for existing
> consumers who don't wire up a real backend doesn't change. Cover it with
> unit tests (see the existing `test/vt_client_tests.cpp` for the style
> already used in this repo) exercising: assigning with
> `storeAsPreferred=true` persists and is re-sent as a preferred
> assignment on the next connection; unassigning with `storeAsPreferred`
> removes it; assigning without `storeAsPreferred` doesn't persist.
> Once implemented and tested, open a PR against this repo with a clear
> description referencing these three TODO locations.

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
