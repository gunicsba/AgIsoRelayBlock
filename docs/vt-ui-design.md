# Virtual Terminal UI Design (Main Screen)

Concrete object pool design for the primary VT screen: 8 relay-state
indicators (4-per-row × 2 rows), 8 dedicated soft keys to toggle them, 1
dedicated soft key to trigger the buzzer, and 17 matching AUX-N functions
(a latching + a momentary variant per relay channel R1–R8, plus one
momentary buzzer function). This refines the general VT/AUX-N notes in
[isobus-protocol.md](isobus-protocol.md) into an actual layout. Naming/icon
*picking* UI (Phase 5) and automation rule UI (Phase 6) build on top of
this later and are not covered here.

Status: **implemented and bench-verified** (Phase 3 — VT screen + SKM —
confirmed rendering and toggling relays on a real VT; Phase 4 — AUX-N —
implemented, end-to-end assignment testing pending real joystick hardware
access). See [roadmap.md](roadmap.md) Phases 3–4 and
[firmware/tools/gen_object_pool.py](../firmware/tools/gen_object_pool.py)
for the actual generator. No bitmaps have been drawn yet (Phase 5) — text
labels are used in their place for now.

## Object pool overview

- **Working Set** object (root of our pool).
- **Data Mask "Main"** — the only mask for MVP:
  - Title/identification text (device name).
  - The relay indicator grid (below).
  - Soft Key Mask assigned: **"Main SKM"** (below).
- Later phases add more masks (naming/icon picker, automation rules) — out
  of scope here, see [roadmap.md](roadmap.md).

## Relay indicator grid (on the Data Mask)

Eight identical widgets, 4 per row × 2 rows (R1–R4 top, R5–R8 bottom), one
per relay channel. First pass used a single row of small 32×32 boxes with
8×8 text and turned out to be barely readable on the bench — bumped up and
reflowed into a grid. Each widget:

- An **Output Rectangle**, 60×60, always outline-drawn (works even on a
  monochrome VT where colour can't be relied on).
  - **OFF:** unfilled / white.
  - **ON:** solid fill (black, or the VT's "active/highlight" colour where
    available) — state is shown by fill, not by hue, so it still reads
    correctly on 2-colour and colour-blind-unfriendly displays.
- An **Output String** "R1".."R8" in 32×32 text, directly under the
  rectangle (not inside it: white-on-black would be fine, but a solid
  black ON fill would swallow black text drawn on top of it, so it stays
  below where it's readable in both states).
- Optional (nice-to-have, not required for MVP): make the widget a
  **Button** object so a touchscreen VT can also toggle it by tapping —
  the real toggle path for non-touch VTs is the SKM/AUX-N below, so this
  is an enhancement, not a dependency.

## Soft Key Mask "Main SKM" (9 keys)

| Key | Action | Icon |
|---|---|---|
| SK1–SK8 | Toggle relay channel 1–8 | Shared relay pictogram + channel number (same icon used by that channel's AUX-N function, so the physical key and the on-screen row look consistent) |
| SK9 | Trigger buzzer (momentary pulse) | Distinct buzzer/speaker pictogram |

Key labels use 32×32 text (bumped up from an initial 8×8 pass that was
"way too small" on the bench — roughly 4× the linear size).

**Compatibility caveat:** not every VT renders 9 soft keys at once — many
show 6 physical keys per mask, some 8, larger ones more. This needs
resolving on the bench (see [Open questions](#open-questions)); options if
9 doesn't fit on one physical VT: a second Soft Key Mask reached via a
"more" key, or (fallback) drop the buzzer to a Data Mask button only on
VTs that can't fit a 9th key.

## AUX-N functions (17 total)

Each relay channel publishes **two** Auxiliary Function Type 2 objects, so
the operator picks whichever behavior fits their equipment in the
tractor's own native AUX-N assignment menu: hold-to-run for spool-valve-
style loads, or toggle-and-stay for lights/pumps/fans. This is a
deliberate choice, not an oversight: the hardware is generic relay
contacts that could drive either kind of load, so offering both lets the
assignment-time choice live where it belongs — with the person wiring up
the equipment — at the cost of a longer list in the tractor's assignment
menu.

**Both variants are declared `BooleanNonLatchingIncreaseValue` (2) at the
protocol level — neither uses `BooleanLatchingOnOff` (0).** Most tractors
only expose momentary (spring-return) physical buttons on the
joystick/armrest, and a tractor's own AUX-N assignment menu generally only
offers inputs and functions of matching type — declaring a function as
latching risks it not even showing up as assignable to a real momentary
button (or behaving inconsistently across tractors that are lenient about
the mismatch). So the "latching" *result* one of the two variants
produces is implemented in our own firmware instead of relied on from the
protocol: it toggles the relay on each rising edge of the (declared
momentary) input and ignores the release, rather than mirroring the input
value straight through.

| # | Function | Type 2 `FunctionType` | Behavior | Label/Icon |
|---|---|---|---|---|
| 1–8 | Relay channel 1–8, toggle | `BooleanNonLatchingIncreaseValue` (2) | Firmware toggles the relay on each press (rising edge), ignores release — a momentary button acts like a latch | Text label **"R{n}"**, **underlined** — same digits as the hold-to-run variant below (an earlier `"R{n}#"` attempt rendered as an unlabeled, clipped "R" in the AUX-N assignment list: its label object had been left at the pre-readability-pass size while everything else got bumped) |
| 9–16 | Relay channel 1–8, hold-to-run | `BooleanNonLatchingIncreaseValue` (2) | Firmware mirrors the input value straight to the relay — on only while the mapped button is held | Text label **"R{n}"**, plain (no underline) — reuses the same label object as that channel's Data Mask indicator |
| 17 | Buzzer | `BooleanNonLatchingIncreaseValue` (2) | Edge-triggered pulse on rising edge; matches SK9's pulse behavior | Own dedicated **"B"** label (short enough it can't clip regardless of the assignment list's designator box size) |

See `handle_aux_function_event` in
[vt_app.cpp](../firmware/main/isobus/vt_app.cpp) for the actual edge
detection / mirroring logic.

All 17 are advertised unconditionally; whether any physical joystick/armrest
button actually gets mapped to one is entirely up to the tractor's own
native AUX-N assignment menu (see [isobus-protocol.md](isobus-protocol.md#auxiliary-control--aux-n-iso-11783-6-annex--iso-11783-7)).
Our only job is to publish 17 distinctly-iconed, correctly-typed functions.

**Type 2, not Type 1:** AgIsoStack++'s own object-pool parser
(`isobus_virtual_terminal_working_set_base.cpp`) logs that
`AuxiliaryFunctionType1` objects are "parsed and validated but NOT
utilized by version 3 or later VTs in making Auxiliary Control
Assignments" — confirmed relevant here since the bench VT reports a
version well into that range. `AuxiliaryFunctionType2` (ISO 11783-6:2018)
is the one that actually works on modern terminals, so that's what's
implemented, even though every example in the vendored library still only
demonstrates the (deprecated-for-this-purpose) Type 1 path.

## Icon design guidelines

- **Format:** monochrome (1-bit) Picture Graphics for broadest VT
  compatibility. A colour variant can be layered on top later for VTs that
  support it, but must stay legible in black & white — colour is
  decoration here, never the only signal (per the "black and white or
  simple colours" brief).
- **Sizes:** ISO 11783-6 defines soft-key icon sizes per VT size class.
  The text-label pass that stands in for icons today reads at 32×32 (up
  from an initial 8×8 that was found to be "way too small" on the bench)
  and the Data Mask indicators are 60×60 — design the actual icon bitmaps
  against those, with a 24×24 fallback for VTs too small to fit them, and
  48×48 variants for larger/newer VTs if the pool format supports
  multiple sizes.
- **Relay icon:** one shared pictogram (simple toggle-switch/relay-coil
  symbol) reused for channels 1–8, distinguished by an overlaid or
  adjacent channel number ("1".."8") — R1..R8 read as *one set*,
  distinguished by number, not by 8 unrelated pictograms.
- **Buzzer icon:** a simple speaker/bell pictogram, visually distinct in
  silhouette from the relay icon at a glance (different action type:
  momentary signal vs. persistent switch), not just distinguished by a
  missing number.
- Actual bitmap artwork is a follow-up implementation task (Phase 3/4) —
  this section is the spec an icon author/tool needs to satisfy, not the
  pixels themselves.

## Interaction / precedence rules

- Three input paths can change a relay's state: Data Mask tap (if the
  touch-toggle enhancement is built), SKM key press, and an AUX-N-mapped
  joystick/armrest button. All three ultimately call the same
  `relay_driver::set_relay(channel, state)` (see
  [architecture.md](architecture.md)) — there's exactly one source of
  truth for relay state, no separate "VT state" vs "AUX-N state".
- Last action wins; no input path is more authoritative than another —
  matches requirement F11 (manual control always able to reclaim an
  automation-driven output).
- SK9/buzzer AUX function is a **momentary pulse**, not a toggle: pressing
  it fires a fixed-duration buzzer pulse; it never reports a "stuck on"
  state back to the VT/AUX-N side.

## Open questions

- [ ] Confirm target VTs' actual soft key counts (6 vs 8 vs more) before
      finalizing whether all 9 keys fit on one SKM or need a second page.
- [ ] Confirm AgIsoStack++'s actual Auxiliary Function "type" enum names
      for latching vs. momentary boolean functions (placeholder terms
      "latching"/"non-latching" used above pending a look at the library API).
- [ ] Decide whether Data Mask indicators are touch-toggleable Button
      objects or read-only Output Rectangles (depends on target VT
      hardware; read-only is the safe default, touch is an enhancement).
- [ ] Produce actual Picture Graphic bitmaps (one shared relay pictogram +
      one buzzer pictogram) once the guidelines above are agreed.
- [ ] Decide object pool versioning/hash strategy so we can tell whether a
      VT already has our current pool cached (see
      [isobus-protocol.md](isobus-protocol.md)).
