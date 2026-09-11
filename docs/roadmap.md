# Roadmap

Phased plan from "empty repo" to a usable, VT-configurable ISOBUS relay
block. Each phase should end in something demonstrable/testable before
moving to the next. Nothing below is implemented yet.

## Phase 0 — Groundwork (docs, this pass)

- [x] Research ISOBUS Block feature set.
- [x] Research ESP32-S3-ETH-8DI-8RO-C hardware capabilities.
- [x] Select ISOBUS stack strategy (AgIsoStack++ vs. hand-rolled).
- [x] Write README, hardware notes, protocol notes, architecture proposal,
      requirements.
- [x] Decide license (WTFPL) and confirm project naming.

## Phase 1 — Hardware bring-up

- [x] Pull exact GPIO mapping from Waveshare's official "Implementation
      Logic" diagram — see [hardware.md](hardware.md#gpio-mapping).
- [x] Get a minimal ESP-IDF project building and flashing on the board
      (blink the RGB LED on `GPIO38`, toggle one relay via the TCA9554PWR
      I²C expander, read one digital input on `GPIO4`) — see
      [firmware/](../firmware) (`main/app_main.cpp` + `main/io/`). Bench
      confirmed: relay expander ACKs and toggles, LED drives.
- [x] Confirm CAN transceiver wiring/TWAI pins (TX=`GPIO17`, RX=`GPIO18`);
      loop back CAN frames using the board's own transceiver (self-test)
      and/or a USB-CAN-A adapter — `io::can_selftest` passes on the bench
      (needed `TWAI_MSG_FLAG_SELF`/`tx_msg.self=1` to request self-reception;
      a plain transmit is not otherwise delivered back to the sender's own
      RX queue). Bonus finding: the bus already carries other real CAN
      traffic in this bench setup, so the self-test drains/ignores foreign
      frames rather than assuming the first RX is its own.
- [x] Cross-check the confirmed pinout against the schematic/demo code
      (image-derived mapping is a strong lead, not a substitute for a
      final sanity check) — **found and fixed a real discrepancy**: the
      Implementation Logic diagram has the I²C SDA/SCL pins backwards.
      Bench-confirmed via an I²C bus scan (`io::i2c_scan`): `GPIO41`=SCL,
      `GPIO42`=SDA (not SDA/SCL as the diagram reads) — see
      [hardware.md](hardware.md#gpio-mapping). Remaining
      [open questions](hardware.md#open-questions) (RTC battery,
      connector/harness choice) still open — those need a decision, not a
      bench check.

## Phase 2 — Bus presence

- [x] Integrate AgIsoStack++ as an ESP-IDF component with the `TWAI`
      hardware driver — vendored as a pinned git submodule at
      [firmware/components/AgIsoStack-plus-plus/upstream](../firmware/components/AgIsoStack-plus-plus/upstream),
      with a thin `idf_component_register`-based wrapper
      ([CMakeLists.txt](../firmware/components/AgIsoStack-plus-plus/CMakeLists.txt))
      since upstream ships a plain-CMake project, not an IDF component.
      `CAN_DRIVER=TWAI` selects `isobus::TWAIPlugin`.
- [x] Implement NAME construction + address claiming ("hello ISOBUS": the
      device shows up with a source address, nothing else yet) — see
      [ecu_identity.cpp](../firmware/main/isobus/ecu_identity.cpp). NAME:
      manufacturer 1407 (AgIsoStack++'s shared non-commercial code),
      industry group Agricultural, device class NonSpecific, function
      IOController (66 — "reporting and/or control unit for external input
      and output channels", the closest standard fit for a generic
      relay/DI block not tied to one implement type), identity number
      derived from the chip's factory MAC for per-unit uniqueness.
- [x] Validate against a bus analyzer or, ideally, a real/simulated
      Virtual Terminal that the device claims correctly and stays claimed —
      **bench-confirmed on real hardware** (2026-09-11): the board's CAN
      transceiver was already sitting on a live bus with other real ISOBUS
      traffic (discovered during Phase 1's CAN self-test), and address
      claiming completed cleanly against it — address 129 claimed in
      ~350ms, no warnings/errors from the stack.

## Phase 3 — Minimal VT presence

- [x] Build the object pool per [vt-ui-design.md](vt-ui-design.md): Data
      Mask with the 8-in-a-row relay state indicators, plus a Soft Key
      Mask with SK1–SK8 (relay toggles) and SK9 (buzzer pulse). There's no
      C++ pool-builder API in AgIsoStack++ (object pools are normally
      authored with an external VT designer GUI and shipped as a raw
      `.iop` binary) — hand-encoded the ISO 11783-6 binary format instead
      via [firmware/tools/gen_object_pool.py](../firmware/tools/gen_object_pool.py),
      using the stack's own parser as ground truth for field layout.
      Validated two ways before ever touching hardware: AgIsoStack++'s
      vendored `iop_parser` example tool (structural parse) and a small
      custom host-side checker calling every object's `get_is_valid()`
      (semantic parse) — both pass clean on all 48 generated objects.
- [x] Wire relay I²C driver so VT/SKM toggles actually switch relays — see
      [vt_app.cpp](../firmware/main/isobus/vt_app.cpp). SK press toggles
      the relay via the same `relay_driver::set_relay` used everywhere
      else (single source of truth, per the precedence rule below), then
      reflects the new state back to the VT via `send_change_fill_attributes`.
- [x] Wire buzzer driver so SK9 fires a momentary buzzer pulse — see
      [buzzer_driver.cpp](../firmware/main/io/buzzer_driver.cpp) (GPIO46,
      one-shot `esp_timer`, non-blocking).
- [x] Wire digital input GPIOs (raw read + debounce), display raw input
      state on the same mask for bring-up/testing purposes. Ended up
      landing as part of Phase 6 rather than here, once there was an
      actual consumer (the limit-switch interlock) for debounced input
      state instead of just a bring-up placeholder -- see
      [Phase 6](#phase-6--automation-rules).
- [ ] Bench-test actual soft key count on target VT hardware/simulator
      and resolve the "9 keys may not fit on one SKM" open question.

**Bench status (2026-09-11): confirmed working end-to-end on a real VT**
(see [images/test iop.png](../images/test%20iop.png) — the pool renders
correctly on [AgIsoVirtualTerminal](https://github.com/gunicsba/AgIsoVirtualTerminal),
R1/R2 show filled/ON, and pressing the on-screen soft keys actually
toggles the relays). Getting there took two real fixes, not just a code
review:
- AgIsoStack++'s internal `std::thread` workers (backed by ESP-IDF's
  pthread component) overflowed the default 3072-byte pthread stack while
  processing an incoming AUX-N message from a real input device already
  on the bus — bumped to 65536 bytes in
  [sdkconfig.defaults](../firmware/sdkconfig.defaults) (generous headroom
  is cheap on this chip; a stack overflow into other memory is a much
  worse failure mode than idle KB).
- The object pool generator emitted the Data Mask *before* the Soft Key
  Mask it references by ID (a forward reference). AgIsoStack++'s own
  parser tolerates this fine, but reordered to a strict bottom-up layout
  in [gen_object_pool.py](../firmware/tools/gen_object_pool.py) anyway
  since it costs nothing and removes a possible edge case for any VT
  implementation that parses in a single linear pass.

**Known issues:**
- ~~The buzzer (SK9) pulse is audible but very quiet.~~ **Resolved:**
  confirmed to be a passive piezo buzzer, which needs a continuously
  driven audio-frequency square wave to sound, not a static DC level.
  Rewrote [buzzer_driver.cpp](../firmware/main/io/buzzer_driver.cpp) to
  drive it via LEDC PWM (~2.7kHz, 50% duty) instead of a plain GPIO pulse.
- The implement intermittently disappears from the VT and needs a VT
  restart to reappear — reproduced on the bench as the VT's control
  function going offline mid-connection. Confirmed to be existing
  behavior of this particular VT (independent of pool content: it
  happened identically whether or not a pool upload was actually
  triggered), not something introduced by this firmware, but still open
  since it affects the real user-visible experience.
  - Made our device's own ISOBUS address stable across reboots (was
    picking whatever was free each time; now requests the same preferred
    address derived from the chip's MAC, confirmed identical across
    repeated reboots on the bench) — see
    [ecu_identity.cpp](../firmware/main/isobus/ecu_identity.cpp). A
    plausible contributing factor, not a confirmed fix.
  - The VT's own log points at a real gap in AgIsoStack++'s vendored
    `VirtualTerminalServer` reference implementation (which
    AgIsoVirtualTerminal is built on): `managedWorkingSetList` is only
    ever matched by comparing the actual `ControlFunction` C++ object
    reference, and there's no timeout-based cleanup anywhere in that file
    -- confirmed by reading the source, not by log inspection. A client
    logged as `"Received a non-status message from a client at address
    N, but they are not connected to this VT"` some time after
    successfully connecting means the *object* the server has cached for
    that client went stale (e.g. if its own network manager briefly loses
    and re-detects the client, creating a new CF object that no longer
    matches the cached one) with no recovery path except restarting the
    whole VT app. Worth reporting upstream to AgIsoStack++ if it
    reproduces with the address now stable.
  - Checked whether our own **client** actively retries reconnecting, by
    reading `isobus_virtual_terminal_client.cpp`'s state machine rather
    than guessing: it does, automatically, with no code change needed --
    `StateMachineState::Failed` resets to `Disconnected` after a fixed
    5-second timeout (already visible in our logs as `"Resetting Failed
    VT Connection"`), `Disconnected` re-checks the partner's address and
    clears the last-seen VT Status timestamp so it genuinely waits for a
    *fresh* status broadcast (not a stale one), and once one arrives it
    walks the entire handshake again from scratch (Working Set Master →
    Get Memory → ... → upload). So there's no missing reconnect logic on
    our side to add. Added `iso::vt_app::is_connected()` (wraps
    `VirtualTerminalClient::get_is_connected()`) and an edge-triggered log
    line in `app_main.cpp`'s main loop instead, purely for visibility --
    the state machine's internal state isn't otherwise exposed, so this
    at least makes the connected/not-connected transitions visible over
    time in future captures, to empirically confirm the retry loop is
    running (or catch it if it somehow isn't) instead of inferring it
    from sparse error/success log lines.
  - Initially suspected the Get Memory response as the blocker, from a VT
    log line reading `"Callback indicated there may be enough memory, but
    since there is overhead associated to object storage it is impossible
    to be sure."` -- **ruled out** by reading `ServerMainComponent`'s
    actual `get_is_enough_memory()` override in `AgIsoVirtualTerminal`:
    it's `return true;` unconditionally, so that log line fires on the
    *success* path (byte 2 of the response is always `0`), not a failure.
    The confusing wording is just an overly cautious debug log next to an
    always-successful check, not a rejection.
  - Got a real answer by capturing our device's own boot log live over
    serial while the VT app was running (`docs/roadmap.md`'s previous
    entries here were all reasoning from the VT's log or from source
    alone -- this was the first capture from our side during an actual
    failed connection attempt): the handshake gets **all the way past**
    VT Status, Working Set Master, Get Memory, Get Number of Softkeys, Get
    Text Font Data, and Get Hardware -- all respond successfully -- then
    stalls specifically on `"[VT]: Get Versions Response Timeout"`, fails,
    and retries forever (`"Resetting Failed VT Connection"`). Traced the
    entry into that state in `isobus_virtual_terminal_client.cpp`: right
    after the Get Hardware response, the client branches on whether
    `objectPools[0].versionLabel` is empty -- non-empty sends Get Versions
    and commits to `WaitForGetVersionsResponse` with no other way out
    (correcting an earlier note here that called this unconditional; it
    isn't, and that's exactly what made it fixable -- see below). We'd set
    this label to a content hash, purely so the VT could skip re-uploading
    an unchanged pool -- a cache-hit optimization we don't actually need
    yet at ~1.6 KB. **Initially worked around on our side** by dropping
    the version label entirely so the client skips Get Versions and goes
    straight to uploading -- confirmed this unblocked the connection (VT
    screen renders fully). **Reverted after connecting**, once a
    same-shaped issue turned up separately in AUX-N joystick assignment:
    judged to be a VT-side bug worth fixing properly in
    `AgIsoVirtualTerminal` (a separate session), not something to
    permanently route around here -- pool caching is the correct
    long-term behavior, so `set_object_pool()` is back to passing the
    content-hash label. Confirmed this doesn't affect the underlying
    `AgIsoVirtualTerminal` question, though: the current source *does*
    implement the Get Versions
    response correctly (`ServerMainComponent::get_versions()` -- lists
    cached `.iopx` files for the client's NAME, returns an empty list
    harmlessly if none exist), so the `.exe` the user's actually running
    predating that handler (per "slightly different from what we have
    locally") remains the leading suspect for why it hadn't answered --
    worth a fresh rebuild/run of that project to confirm, independent of
    this fix. Added `iso::vt_app::is_partner_claimed()` (see below) purely
    so a future
    capture can distinguish "no VT on the bus at all" from "VT present,
    handshake stuck" at a glance without needing a fresh serial capture
    every time. Also added `iso::vt_app::send_version_info()`, which
    pushes `esp_app_get_description()->version` (ESP-IDF's automatic
    `git describe --always --dirty`) to the VT title right after
    connecting -- so which exact firmware build is running is visible on
    the VT screen itself, not just a serial log. **Bench-observed: no
    version text actually showed up next to the title** on the one VT
    session that got far enough to render the full screen -- consistent
    with the same class of bug already suspected in AUX-N assignment
    (the VT silently not honoring a message it should respond to/act on).
    Not chased further this session per the user's call to treat that as
    a VT-side bug for a future session; `send_version_info()`'s call
    itself uses the identical `send_change_string_value` pattern already
    confirmed working for the DI-interlock relay label ("R1!"), so it
    isn't obviously a bug on our side, but that's not proven either --
    worth a fresh serial capture the next time this is revisited, to
    confirm the Change String Value command is actually being sent (vs.
    silently sent-but-ignored by the VT).
  - Also answered a direct question about the VT version we declare:
    AgIsoStack++ hardcodes `SUPPORTED_VT_VERSION = 0x06` in
    `send_working_set_maintenance()` with no public setter, so lowering it
    to match a version-3 VT isn't available as a client-side option
    without patching the vendored submodule. Confirmed from the reference
    server's own source that this doesn't matter anyway: a client
    reporting a higher version than the VT only produces
    `LOG_WARNING("Client N version M is higher than our reported version,
    which is K")` -- the working set is added to `managedWorkingSetList`
    regardless, so it's never the connection blocker.
  - Also checked whether requesting a VT Status broadcast on demand (via a
    PGN request) could help in case the VT only broadcasts on some
    triggering condition: ruled out on both ends. The reference
    `VirtualTerminalServer` broadcasts VT Status unconditionally every
    1000 ms once its `update()`/timer loop is running (no dependency on a
    client being present first), and it never registers a PGN-request
    callback for it, so a request would just be ignored -- moot either
    way, since the new evidence shows VT Status is already being received
    fine (the handshake gets well past that step).

## Phase 4 — AUX-N

- [x] Publish 17 Auxiliary Function Type 2 objects: 8 relay channels ×
      (toggle + hold-to-run variant) + 1 momentary buzzer trigger, all
      declared non-latching/momentary at the protocol level — see
      [vt-ui-design.md](vt-ui-design.md#aux-n-functions-17-total) for why
      two variants per channel, why neither actually uses the "latching"
      `FunctionType` (most tractors only expose momentary buttons, and
      would likely refuse to assign a latching-typed function to one), and
      why Type 2 (not the Type 1 shown in every vendored library example —
      AgIsoStack++'s own parser flags Type 1 as ignored by VT version 3+
      terminals for real assignment).
- [x] Handle Auxiliary Input Status messages to drive relays (and pulse
      the buzzer) from a tractor joystick/armrest button, once assigned
      via the tractor's own AUX-N menu — see
      [vt_app.cpp](../firmware/main/isobus/vt_app.cpp)'s
      `handle_aux_function_event`. The toggle variant is edge-triggered in
      firmware (flips the relay on each rising edge, ignores release) so a
      momentary button produces latching behavior without relying on the
      protocol's own latching type. Builds clean and boots without error
      on the bench; end-to-end assignment (a real joystick button actually
      mapped and driving a relay) still needs a retest once the bench's
      joystick/VT connectivity is stable (see the Phase 3 known issues).
      Assignment testing turned up a real labeling bug: the toggle
      variant's designator label had been left at its pre-readability-pass
      size (16×10, 8×8 font) while every other label got bumped, so it
      rendered as a clipped, unlabeled "R" in the AUX-N assignment list —
      all 8 toggle entries looked identical, an easy way to grab the wrong
      one. Fixed: same "R{n}" text as the momentary variant, sized to
      match, distinguished by an underlined font instead of a suffix
      character; SK1–SK8 got the same "R{n}"/underline treatment for
      consistency (an SKM press already toggles). The buzzer's AUX-N
      designator and SK9 both now say "BZ".

      Assignment testing also turned up a real behavioral bug: with both a
      toggle and a momentary control assigned to the same channel, the
      momentary one always won and forced the relay off, even without
      being pressed. Root cause: the momentary variant mirrored its input
      value straight to the relay, but AUX-N input devices report status
      periodically even while idle, so its own "released" reports kept
      silently overriding whatever the toggle variant had set. Fixed by
      redefining the momentary variant as an override rather than a
      competing direct setter: press saves the relay's current state and
      **inverts** it, release restores the saved state — so a latched-ON
      channel goes OFF while held and back ON on release (and
      symmetrically for a channel that starts OFF). It can no longer fight
      with another control over the same relay, since it only ever acts
      relative to the existing state rather than setting an absolute one.
- [x] Added a second Soft Key Mask page, reached via a next/back key pair
      using the VT's "Change Soft Key Mask" command (the Data Mask itself
      never changes, just which SKM is shown alongside it): page 1 keeps
      SK1–SK8 (toggle) + SK9 (buzzer) as before, plus a new SK10 ("`>>`")
      to switch pages; page 2 has SK1–SK8 as momentary-override keys
      (identical behavior to the AUX-N momentary variant, sharing the same
      per-channel state so pressing either one for a channel doesn't leave
      the other out of sync) plus a back key ("`<<`"). See
      [vt-ui-design.md](vt-ui-design.md#soft-key-masks-two-pages-reached-via-a-nextback-key).

      First attempt had a real logic bug: the back key's event guard used
      `objectID != kSoftkeyBack && keyEvent != Release` to return early,
      which (De Morgan's) actually let the back key through on *every*
      event type (press, held-repeat, and release), sending the mask-
      switch command 2-3 times per single press while everything else
      only fired once on release. Fixed by dropping the exemption
      entirely -- the back key needs no different treatment than SK9/SK10,
      all three act on release only.

      Bench-confirmed after the fix: both directions send exactly once and
      the underlying page switch takes effect immediately (new page's keys
      respond correctly right away) -- but the soft key *labels* sometimes
      don't visually redraw until another key is pressed. Reads as a
      repaint timing quirk on the VT's own side (the switch is confirmed
      sent and applied, this is purely a rendering lag), not something
      firmware controls.
- [x] Verify manual VT control and AUX-N control don't fight each other
      (e.g. last-write-wins, or explicit precedence rule — decide and
      document) — both paths funnel through the same `apply_relay_state`
      helper, which is the single place relay state changes and the VT
      Data Mask gets updated; neither path is more authoritative, matching
      requirement F11.

**Known gap, upstream (2026-09-11):** AUX-N "preferred assignment"
persistence isn't actually implemented in AgIsoStack++, even though the
protocol wiring around it is. Per ISO 11783-6, a function-providing client
(us) is supposed to remember which physical joystick/armrest input the
operator last assigned to each of our functions, and re-announce that as
a "preferred assignment" on every future connection, so the operator
doesn't have to redo the assignment from the tractor's AUX-N menu every
power cycle. `isobus_virtual_terminal_client.cpp` calls
`send_auxiliary_functions_preferred_assignment()` at exactly the right
protocol moments (after `LoadVersionCommand` and after
`EndOfObjectPoolMessage` -- confirmed by the `"Sent preferred assignments
after ..."` log lines), but that function's body is just
`{ Function::PreferredAssignmentCommand, 0 }` next to a
`//! @todo load preferred assignment from saved configuration` comment --
it always announces zero preferred assignments. The save side has the
same gap: when the VT sends an `AuxiliaryAssignmentTypeTwoCommand` with
`storeAsPreferred` set, two more `//! @todo save preferred assignment to
persistent configuration` comments mark where that should be written to
non-volatile storage and never are. Net effect: every reconnect requires
the operator to manually reassign every joystick button to our AUX-N
functions from scratch. Not something to route around in this repo (it's
a real gap in the library, not a workaround-able quirk of our own object
pool) -- see the ready-to-use prompt for fixing it upstream in
[firmware/README.md](../firmware/README.md#known-gaps-in-vendored-agisostack).

## Phase 5 — Naming, icons, persistence

- [ ] Add Input String objects for renaming channels from the VT.
- [ ] Add a small built-in icon set (Picture Graphics) + Object Pointer
      based icon picker per channel.
- [ ] Persist names/icons/AUX-N bookkeeping to NVS; reload on boot. AUX-N
      preferred-assignment persistence specifically also needs the
      upstream AgIsoStack++ gap noted at the end of Phase 4 fixed first --
      there's currently nothing on our side to hook into.
- [ ] Confirm renamed channels show correctly both on our Data Mask *and*
      the tractor's native AUX-N assignment page (this is the specific
      "type it once, it shows up everywhere" behavior being replicated).

## Phase 6 — Automation rules

Phase 5 (naming/icons/persistence) deliberately skipped for now -- it
needs real bitmap artwork (Picture Graphics), which both takes design
effort and grows the object pool / upload time, and isn't needed to prove
out the automation rule concept. Started here with the concrete use case
that actually motivated this phase, rather than the fully generic
`{input, trigger, output, action}` schema up front.

- [x] First rule, hardcoded (not yet VT-configurable): DI{n} acts as a
      limit-switch style safety interlock for relay channel {n} (fixed
      1:1 pairing). While DI{n} is active, channel {n} is forced off
      immediately and refuses to be turned back on by *any* control path
      (SKM, AUX-N toggle, AUX-N momentary) until DI{n} goes inactive --
      and even then it stays off, since a limit switch releasing
      shouldn't by itself resume motion (matches requirement N4's
      safe-default philosophy). See
      [automation/interlock.cpp](../firmware/main/automation/interlock.cpp).
      "Manual override always wins" doesn't apply the usual way here:
      this interlock is the one thing in the system that's allowed to be
      *more* authoritative than manual control, since it represents a
      physical limit, not a competing preference.
- [x] Digital input debounce (closes out the Phase 3 item that had been
      left open) -- see [input_driver.cpp](../firmware/main/io/input_driver.cpp):
      3 consecutive matching samples before a level is accepted, sampled
      every ~20ms (60ms settle time) from the main loop, which now also
      runs at 20ms instead of 200ms for this reason (the LED heartbeat is
      throttled back to its original ~200ms rate independently, so it
      doesn't blink faster).
- [x] Shown on the VT (closes out the other open Phase 3 item -- "display
      raw input state on the same mask for bring-up/testing purposes",
      now genuinely useful rather than just for bring-up): a small
      indicator box under each channel shows its paired DI's raw
      (debounced) state, and the channel's own "R{n}" label gets a `!`
      suffix while disabled, so it's obvious at a glance *why* a channel
      won't respond. See
      [vt-ui-design.md](vt-ui-design.md#digital-input-indicators--limit-switch-interlock).
- [ ] Generalize to a real rule schema (`{input, trigger, output,
      action}`) and VT UI to create/edit/delete rules, if/when a use case
      needs something other than the fixed DI{n}->channel{n} pairing.
- [ ] Persist rules to NVS (moot until the schema above exists --
      today's hardcoded pairing needs no persistence).

**Bench-confirmed and fixed (2026-09-11):**
- With the DI indicator boxes actually visible on a real VT, an unconnected
  digital input was immediately obvious as a real problem, not just
  theoretical: two channels showed disabled/flickering with nothing wired
  to their inputs and nobody touching anything. The original assumption
  ("the board's optocoupler input stage supplies its own bias; no internal
  pull needed") was flagged as unverified from the start and turned out to
  be wrong -- an unconnected input genuinely floats and reads noise.
  Fixed by enabling the ESP32's internal pull-down on all 8 input pins, so
  an unconnected input settles to a defined LOW ("inactive") instead of
  floating -- confirmed on the bench: the previously-flickering channels
  went silent immediately. This matters more than ordinary UI noise would,
  since these inputs drive a safety interlock that force-disables an
  output; an undefined floating state was a real gap, not a cosmetic one.
- The Data Mask's "R{n}" label box was sized for the plain 2-character
  text ("R1") but not the disabled marker ("R1!", 3 characters), which
  didn't fit. Widened the label (and the column spacing to match) using
  screen space that was otherwise going unused.
- The pull-down fix above stopped the flickering but got the *polarity*
  wrong, discovered from a bench report that each DI channel's onboard
  status LED behaves backwards from "COM = active" (brighter toward DGND,
  unaffected by COM). The wiki blocks automated fetches (403), but
  Waveshare's official Arduino demo (`ESP32-S3-POE-ETH-8DI-8RO-C-Demo.zip`,
  `WS_DIN.cpp`) settles it directly: `INPUT_PULLUP` on all 8 channels, plus
  a `DIN_Inverse_Enable` flag inverting the raw reading in software. Each
  channel's optocoupler pulls the isolated-side GPIO LOW when the input is
  actually asserted and leaves it floating otherwise -- active-**low**,
  needing a pull-**up**, not the pull-down from the previous fix (which
  still silenced the floating-noise symptom, since either direction
  defines *some* idle level, just the wrong one here). Switched to
  `GPIO_PULLUP_ENABLE` and inverted the raw-to-logical reading at the same
  single point in `input_driver.cpp` (`gpio_get_level(...) == 0` means
  active), so every caller still just sees `true` = "input active" with
  no changes needed elsewhere. See
  [docs/hardware.md](hardware.md#open-questions).
  **Bench follow-up**: after this fix, unconnected channels (DI4-DI7)
  showed intermittent chatter (active/inactive every few hundred ms to a
  few seconds) -- not yet root-caused. Possibly genuine noise pickup on
  open inputs (the internal ~45kΩ pull-up is weak, and this board switches
  8 relay channels right next to the DI inputs), possibly something else;
  waiting on confirmation of whether those channels are actually wired to
  anything on the bench before adding more debounce/filtering.
- Added a "Momentary Override Safety" checkbox (unchecked/off by default,
  toggled by SK10 on SKM page 2) letting the operator deliberately allow
  the momentary override path (only that one -- toggle paths never bypass
  it) to turn a channel back on past its DI interlock, for cases like an
  auto-mode that normally stops a hydraulic cylinder at a soft limit but
  occasionally needs to push past it on purpose. Deliberately never
  persisted across a reboot, same reasoning as relay outputs already not
  persisting (N4): a bypassed safety limit shouldn't be able to survive a
  power cycle silently. See
  [vt-ui-design.md](vt-ui-design.md#momentary-override-safety-checkbox)
  for the full design and motivating example.

  Implementing this surfaced a real pre-existing gap while touching the
  connection code: nothing re-synced relay/DI/checkbox visual state after
  a VT *reconnect* (as opposed to a full firmware reboot) -- a fresh
  connection re-uploads the object pool, which resets every fill/label to
  the static pool's defaults, but none of our own state resets on a
  reconnect. Fixed by replacing the single-purpose
  `iso::vt_app::send_version_info()` with a general
  `iso::vt_app::resync_display()`, called on every fresh connection, that
  re-pushes the build-version title *and* every relay/DI/checkbox fill and
  label to match actual current state -- reusing the same functions any
  state change already goes through, so there's no separate code path to
  keep in sync.

## Phase 7 — WiFi AP & OTA

- [x] Bring up always-on SoftAP `AgIsoBlock-XXXX` (XXXX = last 2 MAC
      bytes), independent of Ethernet/station state — see
      [net/wifi_ap.cpp](../firmware/main/net/wifi_ap.cpp).
- [x] AP password generation scheme decided and implemented: a random
      12-character password (58-symbol alphabet, no ambiguous characters),
      generated once on first boot and persisted to NVS from then on —
      never a fixed/shared default (N7). Currently the *only* way to
      retrieve it is the serial log at boot (`wifi_ap: SoftAP up:
      SSID=... password=...`); see the follow-up bullet below for the VT
      display work that replaces this.
- [x] Minimal local web UI: firmware upload form + read-only status --
      actually shipped as read/write (mirrors the VT's relay toggles too,
      not just status), a deliberate scope increase past this line's
      original "read-only" per direct request rather than waiting for the
      Phase 9 "richer" version. See
      [net/web_server.cpp](../firmware/main/net/web_server.cpp).
- [x] Wire up OTA against dual OTA partitions with rollback (new image
      must claim its ISOBUS address successfully before being marked
      valid, else auto-revert) -- implemented as a local-upload flow via
      `esp_ota_ops` directly (`POST /ota/upload` streams the raw `.bin`
      body straight into the inactive partition), **not**
      `esp_https_ota` as this section originally said: that API is an
      HTTPS *client* that pulls an image from a remote URL, the wrong
      shape for "upload a file from the browser" (see
      [architecture.md](architecture.md#wifi-ap--ota-planned), corrected
      there too). Required switching from the default single-app
      partition table to a custom dual-OTA one
      ([partitions.csv](../firmware/partitions.csv), 2 MB slots -- see
      that file's own comment for why not the default table's 1 MB) and
      enabling `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`
      (sdkconfig.defaults). `app_main.cpp` confirms the image valid
      (`esp_ota_mark_app_valid_cancel_rollback`) on a successful address
      claim, or proactively rolls back
      (`esp_ota_mark_app_invalid_rollback_and_reboot`) on failure, rather
      than waiting for a crash to trigger the bootloader's own rollback.
- [x] Confirm WiFi AP + TWAI can run concurrently without starving the
      ISOBUS/VT tasks -- bench-confirmed, but only after fixing a real
      bug the concurrency itself exposed: enabling WiFi consumes a large,
      fixed chunk of internal SRAM for its own buffers, which collided
      with AgIsoStack++'s worker threads' 64 KB stacks
      (`CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT`, needed since the Phase 2
      pthread-stack-overflow fix) -- `E (...) pthread: Failed to create
      task!` immediately followed by `abort()`, every single boot,
      100% reproducible. Fixed by redirecting pthread stacks to PSRAM
      (`esp_pthread_set_cfg()` with `stack_alloc_caps = MALLOC_CAP_SPIRAM
      | MALLOC_CAP_8BIT`, called once in `app_main.cpp` right before
      `ecu_identity::init()` spawns AgIsoStack++'s threads) -- this board
      has 8 MB of PSRAM sitting mostly idle, and none of that stack usage
      needs to be DMA-capable or in internal RAM specifically.
      W5500 Ethernet concurrency not tested (not wired up yet at all --
      separate from this phase's scope).
- [x] WiFi status/control panel on the VT itself (direct request, not
      originally scoped in this phase's checklist): the AP's SSID,
      password, `192.168.4.1` IP, and live connected-client count, plus a
      checkbox to disable the AP entirely and an editable field (VT Input
      String) to change the password from the cab. Required a third SKM
      page -- both existing pages were already at the 10-key ceiling, so
      the "Momentary Override Safety" toggle moved from page 2's SK10 to
      the new page 3 (a more natural home next to another device-wide
      setting anyway) to make room for a page2->page3 nav key. See
      [vt-ui-design.md](vt-ui-design.md#wifi-status--control-panel) for
      the full layout and why the password default stayed random-per-unit
      rather than becoming a fixed value.

## Phase 8 — Robustness & polish

- [ ] Decide + implement documented behavior on VT/bus disconnect (hold
      last state vs. force all outputs off), configurable.
- [ ] RGB LED / buzzer status indication (power, CAN activity, VT
      connected/disconnected).
- [ ] DM1 diagnostics for detectable fault conditions.
- [ ] Factory-reset path back to safe defaults (including WiFi AP
      password reset).

## Phase 9 — Stretch goals

- [ ] Richer local web UI (full read/write mirror of VT config, handy for
      bench setup without a terminal attached), over WiFi AP/Ethernet.
- [ ] Legacy (non-N) AUX support, if there's demand from older terminals.
- [ ] Investigate multi-unit expansion (chaining more relay boards over
      CAN) if 8 channels isn't enough for some builds.

## Explicitly not planned

- Task Controller / section control integration.
- Cloud connectivity of any kind.
- AEF certification or any commercial "for-profit" packaging (would
  require obtaining an own SAE manufacturer code instead of using
  AgIsoStack++'s shared non-commercial one — see
  [isobus-protocol.md](isobus-protocol.md#network-management--address-claiming-iso-11783-5)).
