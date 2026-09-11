# ISOBUS / ISO 11783 Protocol Notes

This document captures which parts of the ISO 11783 ("ISOBUS") standard the
firmware needs to implement, and how they map to the features we want
(matching ISOBUS Block: named/iconized relay channels, AUX-N joystick
assignment, VT screen control, sensor-driven automation).

We are **not** implementing ISO 11783 from scratch — see
[architecture.md](architecture.md) for why we plan to build on
[AgIsoStack++](https://github.com/Open-Agriculture/AgIsoStack-plus-plus),
which already implements the pieces below. This document exists so the
project has its own record of *what* is needed and *why*, independent of
whichever library ends up being used.

## Physical / data link layer (ISO 11783-2 / -3, ~SAE J1939)

- CAN 2.0B, 250 kbit/s, 29-bit extended identifiers.
- Provided by the board's onboard isolated CAN transceiver + ESP32-S3's
  built-in TWAI controller.
- 120 Ω termination jumper must be set correctly depending on where in the
  bus topology the device sits (mid-bus vs. end node).

## Network management / address claiming (ISO 11783-5)

Every ECU on the bus needs a unique 64-bit **NAME** and claims a source
address on power-up (and re-claims on conflict). The NAME encodes:

- Manufacturer code (SAE-assigned; see licensing note below)
- Function (implement-specific ECU / "Auxiliary Valve/relay control" class
  fits best — needs to be picked carefully so terminals treat it correctly)
- Device class / industry group (agricultural)
- Self-configurable address flag, ECU/device instance numbers

This is what makes the module show up as a distinct implement ECU on the
tractor's bus at all. Must be implemented correctly or the device won't be
recognized by any terminal.

**Manufacturer code note:** AgIsoStack++'s maintainers allow non-commercial
use of their SAE manufacturer code (1407). Since this is a hobbyist/open
project (not sold for profit), that's usable for development; revisit if
the project is ever productized/sold.

## Virtual Terminal client (ISO 11783-6)

Used to show our UI (channel states, names, icons, naming/config screens)
on whatever ISOBUS Virtual Terminal is already in the tractor cab (John
Deere, Fendt, Valtra, Case IH, CLAAS, Amazone, ...). Key pieces:

- **Object pool**: a VT client uploads a tree of objects (Data/Alarm Masks,
  Soft Key Masks, Output/Input fields, Container, Picture Graphic, etc.) to
  the VT once; the VT then renders it and reports back button
  presses/input changes.
- **Working Set Master / Working Set Maintenance** messages to keep the
  session alive.
- **Output fields** to show each channel's current on/off state.
- **Input Boolean / Soft Key** objects so channels can be toggled directly
  from the VT screen (mirrors ISOBUS Block's "assign from the terminal"
  behavior, and gives us manual control without AUX-N).
  ### Naming and icon selection depend on **`vt-consultation`/Input String and
  Picture Graphic objects**:
- **Input String objects** let the operator type a channel name directly on
  the VT's on-screen keyboard — this is how "type the name" is implemented
  without a phone app or PC.
- **Picture Graphic objects**: object pools are usually static once
  uploaded, so "pick an icon" is implemented by pre-loading a small fixed
  set of icon bitmaps into the pool and letting the operator pick which one
  is *visible/active* per channel (e.g. via Object Pointer objects that
  switch which Picture Graphic a container displays), rather than
  uploading new bitmaps at runtime.
- Object pool must be versioned (VT caches pools by a hash/checksum) so we
  can tell whether the VT already has our latest pool cached or needs a
  fresh upload after a firmware update.

See [vt-ui-design.md](vt-ui-design.md) for the concrete main-screen layout
(8-in-a-row relay indicators + dedicated soft keys + buzzer key).

## Auxiliary control — AUX-N (ISO 11783-6 Annex / ISO 11783-7)

This is the "assign to a joystick/armrest button" feature:

- The device advertises **9 Auxiliary Function objects**: one latching
  boolean per relay channel (8) plus one non-latching/momentary boolean
  for the buzzer trigger — see [vt-ui-design.md](vt-ui-design.md#aux-n-functions-9-total)
  for the full list and icon plan.
- Physical **Auxiliary Input** devices (tractor joystick, armrest buttons)
  advertise their own Auxiliary Input objects.
- The operator maps a function → input using the *tractor's* native AUX-N
  assignment menu (this is why the tractor's existing joystick UI can be
  reused — we don't build our own assignment UI, we just publish functions
  that are compatible with AUX-N).
- The device receives **Preferred Assignment**/assignment messages and then
  runs the actual relay in response to the mapped input's Auxiliary Input
  Status messages.
- Must support both AUX-N (newer, ISO 11783-6 Annex) since that's what
  ISOBUS Block explicitly advertises as working on modern terminals; legacy
  AUX (non-N) is lower priority / stretch goal.

## Task Controller (ISO 11783-9/-10)

**Not required for MVP.** ISOBUS Block is about manual/AUX-N/automation
control of relays, not section control or rate application data logging.
Skip unless a future use case needs TC integration (e.g. logging channel
activations against field/task data).

## Diagnostics (ISO 11783-12 / DM1, DM2, DM3)

- Nice-to-have: broadcast **DM1 (Active Diagnostic Trouble Codes)** for
  conditions we can actually detect: relay driver fault (if/when hardware
  gives feedback), input over-range, CAN bus-off recovery, NVS config
  corruption fallback, etc.
- Not blocking for MVP since this board has no per-channel current/contact
  feedback — real fault detection is limited without extra sensing
  hardware.

## Guidance / speed / other application messages

Not applicable — this device does not need tractor speed, position, or
guidance messages. Skip entirely.

## Summary: minimum viable ISOBUS feature set

To hit functional parity with ISOBUS Block's 8-channel module, the firmware
needs, in priority order:

1. Address claiming with a correct NAME (device is visible on the bus).
2. VT client: object pool upload + Data Mask with the 8-in-a-row relay
   indicators, togglable directly from the VT (see [vt-ui-design.md](vt-ui-design.md)).
3. AUX-N: 9 Auxiliary Function objects (8 relays + buzzer), assignable via
   the tractor's own joystick/armrest menu.
4. On-VT channel naming (Input String) and icon selection (Object
   Pointer + pre-loaded Picture Graphics), persisted locally.
5. Digital input → relay automation rules, configurable from the VT,
   persisted locally (no laptop/app required, matching ISOBUS Block).
6. (Stretch) DM1 diagnostics, Ethernet-based local web config/OTA as a
   secondary (non-ISOBUS) convenience channel.
