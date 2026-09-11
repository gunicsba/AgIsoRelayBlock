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
- [ ] Wire digital input GPIOs (raw read + debounce), display raw input
      state on the same mask for bring-up/testing purposes. Raw read
      already exists from Phase 1 ([input_driver.cpp](../firmware/main/io/input_driver.cpp));
      debounce and showing input state on the VT mask itself are still
      outstanding.
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

## Phase 5 — Naming, icons, persistence

- [ ] Add Input String objects for renaming channels from the VT.
- [ ] Add a small built-in icon set (Picture Graphics) + Object Pointer
      based icon picker per channel.
- [ ] Persist names/icons/AUX-N bookkeeping to NVS; reload on boot.
- [ ] Confirm renamed channels show correctly both on our Data Mask *and*
      the tractor's native AUX-N assignment page (this is the specific
      "type it once, it shows up everywhere" behavior being replicated).

## Phase 6 — Automation rules

- [ ] Design rule schema: `{input, trigger, output, action}` (see
      [architecture.md](architecture.md#configuration--persistence-model-planned)).
- [ ] VT UI to create/edit/delete rules (no laptop/app).
- [ ] Rule engine evaluation loop, with manual override always winning.
- [ ] Persist rules to NVS.

## Phase 7 — WiFi AP & OTA

- [ ] Bring up always-on SoftAP `AgIsoBlock-XXXX` (XXXX = last 2 MAC
      bytes), independent of Ethernet/station state.
- [ ] Decide + implement AP password generation scheme (see
      [architecture.md](architecture.md#wifi-ap--ota-planned) open
      question) — no fixed/shared default password.
- [ ] Minimal local web UI: firmware upload form + read-only status.
- [ ] Wire up `esp_https_ota` against dual OTA partitions with rollback
      (new image must claim its ISOBUS address successfully before being
      marked valid, else auto-revert).
- [ ] Confirm WiFi AP + TWAI + (optional) W5500 can run concurrently
      without starving the ISOBUS/VT tasks (bench test).

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
