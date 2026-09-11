# AgIsoRelayBlock

An open-source, DIY-friendly alternative to commercial ISOBUS relay boxes
(e.g. [ISOBUS Block](https://isobusblock.com/)), built on the off-the-shelf
**Waveshare ESP32-S3-ETH-8DI-8RO-C** industrial relay module.

> Status: **Early firmware, bench-verified through Phase 4, the start of
> Phase 6, Phase 7, and the start of Phase 8.** The device claims an
> ISOBUS address, uploads a three-page Virtual Terminal object pool, its
> on-screen soft keys and AUX-N joystick/armrest functions actually
> switch real relays, a digital input can act as a limit-switch interlock
> for its matching channel, it brings up its own WiFi AP with a local
> status/control page and OTA firmware updates (SSID/password/status also
> shown and editable right on the VT), and it now broadcasts DM1
> diagnostics for the fault conditions it can actually detect -- see
> [Project Status](#project-status) below.

![Object pool rendered on a real Virtual Terminal: 8 relay indicators in a 2x4 grid with DI interlock boxes (R3 shown disabled as "R3!" with its DI box filled), the Momentary Override Safety checkbox, the WiFi status panel (SSID/password/IP/connected clients), and the 10-key Soft Key Mask (R1-R8 toggle, BZ buzzer, >> next page)](images/main%20screen.png)

## Why

ISOBUS terminals in modern tractors (John Deere, Fendt, Valtra, Case IH,
CLAAS, ...) already provide a screen and AUX-N joystick/armrest buttons.
Most older or 3rd-party implements only have a dumb toggle-switch box, so
every implement needs its own unlabeled switches and the tractor's built-in
controls go unused.

Commercial products like ISOBUS Block solve this with a small relay ECU that
plugs into the tractor's ISOBUS (ISO 11783) connector: outputs are named,
given icons, and mapped to any AUX-N joystick button or the Virtual Terminal
screen, with a couple of onboard inputs available for simple automations.

**Goal of this project:** reproduce that functionality as open-source
firmware for cheap, widely-available industrial ESP32 relay hardware, so
anyone can build or modify their own ISOBUS relay/input block.

## Target hardware

[Waveshare ESP32-S3-ETH-8DI-8RO-C](https://www.waveshare.com/esp32-s3-eth-8di-8ro-c.htm)
("C" = CAN variant), an industrial DIN-rail module built around an
ESP32-S3-WROOM-1U-N16R8:

> **This board ships in several look-alike variants — only the "-C" (CAN)
> one works for ISOBUS.** Waveshare also sells an `ESP32-S3-ETH-8DI-8RO`
> (no "C") with **RS485 instead of CAN**, and a PoE Ethernet variant
> (`ESP32-S3-POE-ETH-8DI-8RO-C`, still CAN, still fine). When buying, check
> for the CAN transceiver + screw terminal and the "-C" suffix — see
> [docs/hardware.md#variants](docs/hardware.md#variants).

- 8x relay outputs (≤10 A @ 250 VAC / 30 VDC, 1NO+1NC, optocoupler isolated)
- 8x digital inputs (5–36 V, passive/active, bi-directional optocoupler isolated)
- Onboard **isolated CAN transceiver** (this is our ISOBUS physical layer),
  with a jumper-selectable 120 Ω termination resistor
- W5500 10/100 Ethernet + WiFi/BLE (used for config/OTA/diagnostics, not for ISOBUS)
- USB-C (power, flashing, debug), RTC, buzzer, WS2812 RGB status LED, TF
  card slot, screw terminals, DIN-rail ABS enclosure
- 7–36 V DC screw-terminal power input (12 V/24 V tractor electrics compatible)

Full details and open questions in [docs/hardware.md](docs/hardware.md).

## Feature checklist (parity target vs. ISOBUS Block)

| Feature | ISOBUS Block | AgIsoRelayBlock plan |
|---|---|---|
| ISO 11783 ECU, address claim | Yes | Yes ([docs/isobus-protocol.md](docs/isobus-protocol.md)) |
| Shows up on tractor's Virtual Terminal | Yes | Yes (VT client + object pool, see [docs/vt-ui-design.md](docs/vt-ui-design.md)) |
| Name each channel, pick an icon, from the terminal | Yes | Yes (VT input objects, no PC/app needed) |
| Assign any channel to a joystick/armrest button (AUX-N) | Yes | Yes (17 AUX-N functions: 8 relays × toggle+momentary variants + buzzer) |
| Sensor input → automatic relay control, configured on-screen | Yes (8ch model) | Yes (rule engine, VT-configurable) |
| Runs on 12 V / 24 V tractor power | Yes | Yes (board supports 7–36 V) |
| Industrial isolation (opto + power) | Yes | Yes (board provides this in hardware) |
| Channel count | 8 or 32 | 8 (matches this board); more via CAN-connected expansion later |
| Firmware updates | Vendor-managed | Self-hosted OTA over always-on WiFi AP (`AgIsoBlock-XXXX`) and/or Ethernet |
| OEM/private label, warranty, support | Commercial | Not applicable (open source, no warranty) |

See [docs/requirements.md](docs/requirements.md) for the detailed functional
and non-functional requirements this maps to.

## Planned architecture (short version)

- **Framework:** ESP-IDF (native), C++17.
- **ISOBUS/J1939 stack:** [AgIsoStack++](https://github.com/Open-Agriculture/AgIsoStack-plus-plus)
  (MIT-licensed, has a built-in ESP32 **TWAI** hardware driver, VT client,
  and AUX-N support already implemented — avoids re-implementing ISO 11783
  from scratch).
- **CAN:** ESP32-S3 built-in TWAI controller → onboard isolated CAN
  transceiver → ISOBUS connector (250 kbit/s, 29-bit extended ID).
- **I/O:** thin driver layer mapping AgIsoStack++ output/input requests to
  the board's relay and digital-input GPIOs.
- **Config storage:** channel names/icons, AUX-N assignments and automation
  rules persisted in NVS flash.
- **WiFi:** always-on SoftAP named `AgIsoBlock-XXXX` (XXXX = last 2 bytes
  of the device's MAC, hex), used for OTA updates and local config — never
  for ISOBUS traffic.
- **Ethernet (W5500):** not part of the ISOBUS link; an alternate path for
  the same local web UI, logging, and OTA updates.

Full write-up in [docs/architecture.md](docs/architecture.md).

## Project status

Bench-verified through Phase 4 (plus the first slice of Phase 6) of
[docs/roadmap.md](docs/roadmap.md) on real hardware (see
[firmware/README.md](firmware/README.md) for the full detail and
bug-fix history):

1. ~~Pin down exact GPIO mapping~~ — done, and corrected a real error in
   Waveshare's own diagram (I²C SDA/SCL were swapped) along the way.
2. ~~Get AgIsoStack++ building for ESP32-S3 (TWAI driver) with a "hello
   ISOBUS" address-claim-only example.~~ — done; claims a real address
   against a live bus in ~350ms, now stable across reboots too.
3. ~~Minimal VT object pool: 8 on/off indicators, no naming yet.~~ — done;
   hand-encoded (no external pool designer tool used) and confirmed
   rendering correctly on a real VT.
4. ~~Wire relay/DI GPIOs into the stack.~~ — done: relays are driven by
   two Soft Key Mask pages and 17 AUX-N functions; all 8 digital inputs
   are debounced and each shown on screen next to its channel.
5. ~~AUX-N support.~~ — done: 17 Auxiliary Function Type 2 objects (a
   toggle + a momentary-invert-and-restore variant per relay channel, plus
   a momentary buzzer trigger), confirmed rendering and driving relays on
   a real VT.
6. On-screen channel naming/icon picker + persistence — **deliberately
   skipped for now**, needs real bitmap artwork (Picture Graphics), which
   both takes design effort and grows the object pool / upload time.
7. Input → output automation rules — **started**: the first, concrete use
   case (digital input DI{n} as a limit-switch interlock forcing relay
   channel {n} off) is done, plus a "Momentary Override Safety" checkbox
   letting an operator deliberately bypass it with a momentary button/key
   when they need to; a general VT-configurable rule schema is not.
8. ~~WiFi AP (`AgIsoBlock-XXXX`) + OTA update path.~~ — done: always-on
   SoftAP with a randomly-generated, NVS-persisted password, and a local
   web page (status + relay toggles mirroring the VT, plus a firmware
   upload form) with rollback if the new image fails to claim its ISOBUS
   address. SSID/password/status are also shown (and the password is
   editable) right on the VT itself.

   <img src="images/web%20ui.jpg" alt="local web UI on a phone: status page mirroring the VT, R3 shown ACTIVE/grayed-out under DI Interlock" width="300"> <img src="images/web%20ota.jpg" alt="local web UI OTA upload page on a phone" width="300">
9. Polish, ~~diagnostics (DM1).~~ — DM1 done: broadcasts Active Diagnostic
   Trouble Codes for VT-connection-lost and relay-I2C-write-failure, the
   two conditions this hardware can actually detect right now (CAN
   bus-off monitoring and physical relay-state feedback are still open).
   General polish is not.

Known open issues: the tractor's soft key labels sometimes don't visually
refresh when switching between SKM pages until another key is pressed,
even though the underlying page switch takes effect immediately (reads
as a VT-side repaint quirk, not a firmware bug -- the switch
command is confirmed sent and the new page's keys already respond
correctly); the implement sometimes disappears from the VT and needs a VT
restart (traced to a likely gap in AgIsoStack++'s own vendored VT server
reference implementation, not this firmware -- see
[docs/roadmap.md](docs/roadmap.md#phase-3--minimal-vt-presence)).

## Disclaimer

This is an independent, hobbyist open-source project. It is not affiliated
with, endorsed by, or derived from ISOBUS Block, ETERVO Oy, or any tractor
or terminal manufacturer. "ISOBUS" and "ISO 11783" refer to the open AEF/ISO
standard that any compliant device (this one included) can implement.
Brand names mentioned are the property of their respective owners.

## License

WTFPL (Do What The F*ck You Want To Public License) — see [LICENSE](LICENSE).
No warranty, no restrictions; do whatever you want with this code.

## Contributing

Not yet open for contributions — the project is still in the planning
stage. Feel free to open an issue with hardware notes, protocol corrections,
or use cases you'd like covered.
