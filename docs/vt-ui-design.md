# Virtual Terminal UI Design (Main Screen)

Concrete object pool design for the primary VT screen: 8 relay-state
indicators in a row, 8 dedicated soft keys to toggle them, 1 dedicated
soft key to trigger the buzzer, and 9 matching AUX-N functions (R1–R8 +
buzzer). This refines the general VT/AUX-N notes in
[isobus-protocol.md](isobus-protocol.md) into an actual layout. Naming/icon
*picking* UI (Phase 5) and automation rule UI (Phase 6) build on top of
this later and are not covered here.

Status: design proposal, not yet implemented (see [roadmap.md](roadmap.md)
Phases 3–4). No bitmaps have been drawn yet — this document specifies what
they need to satisfy.

## Object pool overview

- **Working Set** object (root of our pool).
- **Data Mask "Main"** — the only mask for MVP:
  - Title/identification text (device name).
  - The 8-in-a-row relay indicator strip (below).
  - Soft Key Mask assigned: **"Main SKM"** (below).
- Later phases add more masks (naming/icon picker, automation rules) — out
  of scope here, see [roadmap.md](roadmap.md).

## 8-in-a-row relay indicators (on the Data Mask)

Eight identical widgets placed side by side across the mask, one per relay
channel. Each widget:

- An **Output Rectangle**, fixed square size, always outline-drawn (works
  even on a monochrome VT where colour can't be relied on).
  - **OFF:** unfilled / white.
  - **ON:** solid fill (black, or the VT's "active/highlight" colour where
    available) — state is shown by fill, not by hue, so it still reads
    correctly on 2-colour and colour-blind-unfriendly displays.
- An **Output String** "R1".."R8" always visible (inside or directly under
  the rectangle) — channel identity must never depend on colour alone.
- Optional (nice-to-have, not required for MVP): make the widget a
  **Button** object so a touchscreen VT can also toggle it by tapping —
  the real toggle path for non-touch VTs is the SKM/AUX-N below, so this
  is an enhancement, not a dependency.

## Soft Key Mask "Main SKM" (9 keys)

| Key | Action | Icon |
|---|---|---|
| SK1–SK8 | Toggle relay channel 1–8 | Shared relay pictogram + channel number (same icon used by that channel's AUX-N function, so the physical key and the on-screen row look consistent) |
| SK9 | Trigger buzzer (momentary pulse) | Distinct buzzer/speaker pictogram |

**Compatibility caveat:** not every VT renders 9 soft keys at once — many
show 6 physical keys per mask, some 8, larger ones more. This needs
resolving on the bench (see [Open questions](#open-questions)); options if
9 doesn't fit on one physical VT: a second Soft Key Mask reached via a
"more" key, or (fallback) drop the buzzer to a Data Mask button only on
VTs that can't fit a 9th key.

## AUX-N functions (9 total)

| # | Function | Behavior | Icon |
|---|---|---|---|
| 1–8 | Relay channel 1–8 | Latching boolean (on/off, holds state) — a relay is genuinely either on or off, matching ISOBUS Block's persistent-switch behavior | Shared relay pictogram + channel number overlay/label, same as the SKM key |
| 9 | Buzzer | Non-latching / momentary boolean (fires on press, doesn't need a second press to "turn off") — it's a signal, not a stored state | Distinct buzzer/speaker pictogram |

All 9 are advertised unconditionally; whether any physical joystick/armrest
button actually gets mapped to one is entirely up to the tractor's own
native AUX-N assignment menu (see [isobus-protocol.md](isobus-protocol.md#auxiliary-control--aux-n-iso-11783-6-annex--iso-11783-7)).
Our only job is to publish 9 distinctly-iconed, correctly-typed functions.

## Icon design guidelines

- **Format:** monochrome (1-bit) Picture Graphics for broadest VT
  compatibility. A colour variant can be layered on top later for VTs that
  support it, but must stay legible in black & white — colour is
  decoration here, never the only signal (per the "black and white or
  simple colours" brief).
- **Sizes:** ISO 11783-6 defines soft-key icon sizes per VT size class;
  design against the smallest common size (24×24 px) first, with
  32×32/48×48 variants for larger/newer VTs if the pool format supports
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
