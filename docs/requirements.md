# Requirements

Functional and non-functional requirements for AgIsoRelayBlock, derived from
the ISOBUS Block product (see [isobusblock.com](https://isobusblock.com/))
and adapted to what the Waveshare ESP32-S3-ETH-8DI-8RO-C board can
physically do. See [docs/hardware.md](hardware.md) for board capabilities
and [docs/isobus-protocol.md](isobus-protocol.md) for the protocol pieces
each requirement depends on.

Status legend: 🎯 target for MVP · 🧊 stretch/later · ❌ out of scope

## Functional requirements

### Bus presence

- 🎯 **F1.** Device claims a valid ISO 11783 NAME/address on power-up and
  re-claims correctly on conflict.
- 🎯 **F2.** Device is recognized as an implement ECU by common Virtual
  Terminals (John Deere, Fendt, Valtra, Case IH, CLAAS — test against
  whatever hardware/simulator is available; can't test every brand).

### Relay outputs

- 🎯 **F3.** 8 independent relay channels, each individually on/off
  controllable.
- 🎯 **F4.** Each channel visible as one of 8 in-a-row state indicators on the
  main Virtual Terminal Data Mask, plus toggleable via 8 dedicated Soft
  Key Mask keys (SK1–SK8) — see [vt-ui-design.md](vt-ui-design.md).
- 🎯 **F5.** Each channel can be assigned, from the tractor's *own* native
  AUX-N assignment menu, to any joystick/armrest button (device just needs
  to publish correct Auxiliary Function objects; no custom assignment UI
  is built by us). 9 functions total are published: 8 relay channels +
  1 buzzer trigger (F24).
- 🎯 **F6.** Channel state changes (from VT, AUX-N, or automation) reflect
  back to the VT display within a reasonable UI-refresh latency.
- 🧊 **F7.** Per-channel output "type" (latching / momentary / pulse
  duration) configurable from the VT, for e.g. spool valve style momentary
  triggers vs. lighting-style latching switches.
- 🎯 **F24.** A dedicated Soft Key (SK9) and matching 9th AUX-N function
  trigger the onboard buzzer as a momentary pulse (not a persisted on/off
  state) — see [vt-ui-design.md](vt-ui-design.md#soft-key-mask-main-skm-9-keys).

### Digital inputs / automation

- 🎯 **F8.** 8 digital inputs readable, debounced, each independently
  configurable as passive (dry contact) or active (NPN/PNP) per the
  board's supported wiring modes.
- 🎯 **F9.** Any input can be configured, from the VT, to automatically
  drive any output (e.g. "input 3 rising edge → toggle channel 5") without
  external tools.
- 🎯 **F10.** Automation rules persist across power cycles.
- 🎯 **F11.** Manual control (VT or AUX-N) always able to override/take
  back control from an automation-driven state (matches ISOBUS Block's
  "grab the joystick and you are back in manual").

### Naming & customization

- 🎯 **F12.** Each channel can be renamed from the VT (on-screen keyboard,
  no PC/app), and that name is shown consistently on both the VT screen
  and wherever the tractor's own AUX-N assignment page shows function
  names.
- 🎯 **F13.** Each channel can have an icon picked from a small built-in
  set, shown on the VT (channels 1–8 share one relay pictogram + number,
  per [vt-ui-design.md](vt-ui-design.md#icon-design-guidelines)).
- 🎯 **F14.** Names/icons persist across power cycles and firmware updates
  (config migration story needed once the schema stabilizes).

### Power & electrical

- 🎯 **F15.** Operates correctly across the board's full 7–36 V input range
  (covers both 12 V and 24 V tractor electrics).
- 🎯 **F16.** Relies on the board's existing opto/power isolation; firmware
  makes no assumptions that would defeat it (e.g. no bridging isolated and
  non-isolated grounds in software-controlled ways — this is mostly a
  hardware property, but firmware must not, e.g., require a shared ground
  reference that isn't actually isolated).

### Diagnostics & robustness

- 🧊 **F17.** Broadcast DM1 for detectable fault conditions (CAN bus-off,
  config corruption fallback, VT connection loss).
- 🎯 **F18.** Defined, documented behavior for relay outputs when the
  ISOBUS/VT connection is lost (configurable: hold last state vs. force
  all-off) — safety-relevant, needs explicit decision, not an accident of
  implementation.
- 🧊 **F19.** Local status indication via onboard RGB LED for
  power/CAN/VT-connected states (board already has the hardware for
  this). Distinct from F24's buzzer, which is a VT/AUX-N-triggered user
  action, not an internal status indicator.

### Non-ISOBUS convenience (secondary, does not affect ISOBUS behavior)

- 🧊 **F20.** Optional local web UI over the onboard Ethernet port for
  bench configuration/diagnostics, read-only mirror of VT-editable config.
- 🎯 **F21.** OTA firmware update, reachable over the device's own WiFi AP
  (and, if connected, the onboard Ethernet port). No serial/USB-C cable
  needed to update a unit already installed on a machine.
- 🎯 **F23.** Device always runs a self-hosted WiFi access point (in
  addition to, not instead of, station/Ethernet connectivity if
  configured) so OTA/config is reachable even with no existing network
  present. SSID: `AgIsoBlock-XXXX`, where `XXXX` is 4 uppercase hex
  characters derived from the last 2 bytes of the device's WiFi station
  MAC address, so every unit has a unique, predictable, label-free SSID
  (mirrors the common ESP32 `ESP_XXXXXX` default-AP pattern). No default
  password is a security requirement — see N7.
- ❌ **F22.** Cloud connectivity (Waveshare Cloud, MQTT, etc.) — explicitly
  out of scope; this project is local/offline by design like the physical
  ISOBUS bus it serves.

### Explicitly out of scope

- ❌ Task Controller (section control, rate application logging) — not
  part of what ISOBUS Block does either.
- ❌ 32-channel variant — this board only has 8 relays/8 inputs; a 32ch
  version would need different/expanded hardware (possibly multiple
  boards networked over CAN) and is not planned initially.
- ❌ AEF conformance certification — this is a hobbyist project, not a
  certified product.

## Non-functional requirements

- 🎯 **N1. Open source.** All firmware source available under an OSI
  license (see [README](../README.md) licensing section); no cloud
  dependency required for core function.
- 🎯 **N2. Reproducible builds.** Anyone with the same board should be able
  to clone, build, and flash without proprietary tools beyond ESP-IDF.
- 🎯 **N3. No app/laptop required in the field.** All day-to-day
  configuration (naming, icons, automation rules, AUX-N assignment) must
  be possible entirely from the tractor's existing VT and joystick — this
  is the core value proposition being replicated from ISOBUS Block.
- 🎯 **N4. Safe defaults.** On first boot / factory reset, all outputs
  default to off, and channels have generic default names so an
  unconfigured unit fails safe.
- 🧊 **N5. Documented, testable protocol behavior.** Where practical,
  validate against an ISOBUS bus analyzer/simulator (e.g. Waveshare
  USB-CAN-A, or a real terminal) before claiming a feature "done".
- 🎯 **N6. No warranty / as-is.** Since this replaces a commercially
  warrantied product with a hobbyist one, this must be extremely clear in
  user-facing docs (electrical/relay safety is the user's responsibility,
  same cautions as in the board's own safety instructions).
- 🎯 **N7. Safe OTA/AP defaults.** The WiFi AP used for OTA/config
  (`AgIsoBlock-XXXX`) must require a per-device or setup-time-generated
  password (never an unauthenticated open AP), and OTA updates must be
  authenticated and image-verified (signed or checksum-and-confirm) before
  a device in the field will accept them — a relay box that controls
  machinery must not accept arbitrary firmware from anyone in WiFi range.
