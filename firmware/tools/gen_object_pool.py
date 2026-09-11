#!/usr/bin/env python3
"""Generates the ISO 11783-6 VT object pool binary (.iop) for the main
screen, per docs/vt-ui-design.md: a Data Mask with an 8-in-a-row relay
state indicator strip, a "Momentary Override Safety" checkbox, a WiFi
status/control panel, three Soft Key Mask pages chained via next/back keys
(page 1: SK1-SK8 relay toggles + SK9 buzzer + SK10 next-page; page 2:
SK1-SK8 momentary override + SK9 back + SK10 next-page; page 3: SK1 WiFi
AP toggle + SK2 override-safety toggle + SK9 back), and 17 Auxiliary
Function Type 2 objects (a toggle + a momentary-override variant per relay
channel, plus one momentary buzzer function) for AUX-N joystick/armrest
assignment.

Auxiliary Function Type 2, not Type 1: AgIsoStack++'s own parser
(isobus_virtual_terminal_working_set_base.cpp) logs that Type 1 objects
are "parsed and validated but NOT utilized by version 3 or later VTs in
making Auxiliary Control Assignments" -- Type 2 is the one that actually
works on modern terminals.

There's no C++ pool-builder API in AgIsoStack++ (object pools are normally
authored with an external VT designer GUI and shipped as a raw .iop
binary) -- this script hand-encodes the same ISO 11783-6 binary format
directly, using AgIsoStack++'s own parser
(isobus/src/isobus_virtual_terminal_working_set_base.cpp) as the ground
truth for field layout. Validate any change to this script by rebuilding
and running the `iop_parser` tool vendored in
components/AgIsoStack-plus-plus/upstream/examples/virtual_terminal/iop_parser_tester
against the generated file -- see firmware/README.md.

Usage: python gen_object_pool.py <output.iop> <output_ids.hpp>
"""
import struct
import sys

NULL_OBJECT_ID = 0xFFFF

# ISO 11783-6 VirtualTerminalObjectType values (see
# isobus_virtual_terminal_objects.hpp's VirtualTerminalObjectType enum).
T_WORKING_SET = 0
T_DATA_MASK = 1
T_SOFT_KEY_MASK = 4
T_KEY = 5
T_OUTPUT_STRING = 11
T_INPUT_STRING = 8
T_OUTPUT_RECTANGLE = 14
T_FONT_ATTRIBUTES = 23
T_LINE_ATTRIBUTES = 24
T_FILL_ATTRIBUTES = 25
T_AUXILIARY_FUNCTION_TYPE_2 = 31

# AuxiliaryFunctionType2::FunctionType (ISO 11783-6:2018 table J.5) values.
# AUX_FUNC_LATCHING_ON_OFF (0) is deliberately unused: see the comment in
# build_pool() on why every function here is declared momentary instead,
# even the one that ends up behaving like a latch.
AUX_FUNC_LATCHING_ON_OFF = 0
AUX_FUNC_NON_LATCHING_MOMENTARY = 2

# Standard VT colour palette indices actually used here.
COLOUR_BLACK = 0
COLOUR_WHITE = 1

# --- Object IDs -------------------------------------------------------
ID_WORKING_SET = 1000
ID_DATA_MASK = 1100
ID_TITLE_STRING = 1101
# Reserved character count for ID_TITLE_STRING -- see build_pool()'s comment
# on this same constant for why it's wider than the static "AgIsoRelayBlock"
# text (firmware overwrites it at connect time with a build version suffix).
TITLE_MAX_CHARS = 44
ID_SOFT_KEY_MASK = 1200  # page 1: SK1-SK8 toggle, SK9 buzzer, SK10 next-page
ID_SOFT_KEY_MASK_2 = 1201  # page 2: SK1-SK8 momentary override, SK9 back, SK10 next-page
ID_SOFT_KEY_MASK_3 = 1202  # page 3: WiFi status/control panel, SK9 back

# WiFi status/control panel (Data Mask, visible on every SKM page, same as
# the override checkbox above it): mirrors net::wifi_ap.hpp. See
# docs/vt-ui-design.md#wifi-status--control-panel.
ID_WIFI_ENABLED_RECT = 1142
ID_WIFI_ENABLED_LABEL = 1143
ID_WIFI_SSID_LABEL = 1144
ID_WIFI_PASSWORD_INPUT = 1145
ID_WIFI_IP_LABEL = 1146
ID_WIFI_CLIENTS_LABEL = 1147
ID_WIFI_ENABLED_FILL = 1941
# Reserved character count for ID_WIFI_PASSWORD_INPUT -- WPA2-PSK allows up
# to 63 ASCII characters; 32 comfortably covers both the 12-character
# generated default and any realistic operator-chosen password without
# reserving the full 63 (see TITLE_MAX_CHARS's comment for the same
# reserved-wider-than-default-content reasoning).
WIFI_PASSWORD_MAX_CHARS = 32

ID_SOFTKEY_WIFI_TOGGLE = 1264
ID_SOFTKEY_WIFI_TOGGLE_LABEL = 1265
ID_SOFTKEY_NEXT_3 = 1266  # page 2 -> page 3
ID_SOFTKEY_BACK_3 = 1267  # page 3 -> page 2

ID_FONT = 1900
ID_FONT_LARGE = 1901  # 32x32 -- Data Mask indicators + soft key labels
ID_FONT_LARGE_UNDERLINE = 1902  # same, underlined -- marks the AUX-N toggle variant
ID_LINE_ATTR = 1910

FONT_STYLE_UNDERLINED = 0x04  # FontAttributes::FontStyleBits::Underlined bit


def relay_rect_id(channel):  # channel: 1-8
    return 1110 + channel


def relay_label_id(channel):
    return 1120 + channel


def relay_fill_attr_id(channel):
    return 1920 + channel


def di_rect_id(channel):  # channel: 1-8 -- digital input state indicator
    return 1130 + channel


def di_fill_attr_id(channel):
    return 1930 + channel


def aux_latch_function_id(channel):  # channel: 1-8
    return 1500 + channel


def aux_momentary_function_id(channel):  # channel: 1-8
    return 1520 + channel


def aux_latch_label_id(channel):
    return 1540 + channel


ID_AUX_BUZZER_FUNCTION = 1560
ID_AUX_BUZZER_LABEL = 1561


def softkey_id(key_number):  # key_number: 1-9 (1-8 relays, 9 buzzer), 10 = next-page
    return 1210 + key_number


def softkey_label_id(key_number):
    return 1230 + key_number


def softkey2_id(channel):  # channel: 1-8, momentary override page
    return 1250 + channel


ID_SOFTKEY_BACK = 1260
ID_SOFTKEY_BACK_LABEL = 1261
ID_SOFTKEY_OVERRIDE = 1262  # page 2, SK10: toggles ID_OVERRIDE_CHECKBOX_FILL
ID_SOFTKEY_OVERRIDE_LABEL = 1263

# "Momentary Override Safety" checkbox (Data Mask, visible on both SKM
# pages): unfilled = interlock enforced as normal (default, safe), filled =
# a momentary AUX-N/SKM press is allowed to turn a channel back on even
# while its DI interlock has it disabled. Never persisted -- always starts
# unfilled/off at boot, matching the object pool's own static default, so
# the safety feature can never be silently bypassed by a power cycle. See
# docs/vt-ui-design.md.
ID_OVERRIDE_CHECKBOX_RECT = 1140
ID_OVERRIDE_CHECKBOX_LABEL = 1141
ID_OVERRIDE_CHECKBOX_FILL = 1940


def u16(value):
    return struct.pack("<H", value & 0xFFFF)


def i16(value):
    return struct.pack("<h", value)


def object_header(object_id, object_type):
    return u16(object_id) + bytes([object_type])


def child_ref(object_id, x, y):
    return u16(object_id) + i16(x) + i16(y)


def macro_list(macro_events=()):
    # We don't use macros for this MVP pool; count byte + zero entries.
    assert not macro_events
    return bytes([0])


def make_working_set(active_mask_id, designator_children):
    # ISO 11783-6 requires at least one designator child here (an object
    # that fits inside a Soft Key designator, shown when a VT lets the
    # operator pick among multiple Working Sets) -- AgIsoStack++ enforces
    # this via WorkingSet::MIN_OBJECT_LENGTH (16 bytes: the 10-byte fixed
    # header plus at least one 6-byte child reference).
    background_colour = COLOUR_WHITE
    selectable = 1
    macros_to_follow = 0
    languages_to_follow = 0
    body = (
        bytes([background_colour, selectable])
        + u16(active_mask_id)
        + bytes([len(designator_children), macros_to_follow, languages_to_follow])
        + b"".join(child_ref(oid, x, y) for oid, x, y in designator_children)
    )
    return object_header(ID_WORKING_SET, T_WORKING_SET) + body


def make_data_mask(children):
    # children: list of (object_id, x, y)
    # Note: unlike Output*/FontAttributes/LineAttributes/FillAttributes,
    # the macro count here is a header FIELD (already 0 below), not a
    # separate trailing byte -- with 0 macros, no macro bytes follow at all.
    background_colour = COLOUR_WHITE
    body = (
        bytes([background_colour])
        + u16(ID_SOFT_KEY_MASK)
        + bytes([len(children), 0])  # childrenToFollow, macrosToFollow
        + b"".join(child_ref(oid, x, y) for oid, x, y in children)
    )
    return object_header(ID_DATA_MASK, T_DATA_MASK) + body


def make_output_string(object_id, width, height, text, font_id=ID_FONT):
    background_colour = COLOUR_WHITE
    options = 0
    variable_reference = NULL_OBJECT_ID  # use the static Value field below
    justification = 0  # left/top justified
    text_bytes = text.encode("ascii")
    body = (
        u16(width)
        + u16(height)
        + bytes([background_colour])
        + u16(font_id)
        + bytes([options])
        + u16(variable_reference)
        + bytes([justification])
        + u16(len(text_bytes))
        + text_bytes
        + macro_list()
    )
    return object_header(object_id, T_OUTPUT_STRING) + body


def make_input_string(object_id, width, height, length, initial_value, font_id=ID_FONT, enabled=1):
    # Unlike Output String, `length` is a fixed reserved size, not just the
    # current text's length -- the operator can type up to that many
    # characters. `initial_value` must already be exactly `length` bytes
    # (space-padded), same convention as the title string.
    background_colour = COLOUR_WHITE
    input_attributes = NULL_OBJECT_ID  # no character-set restriction
    options = 0
    variable_reference = NULL_OBJECT_ID  # use the static Value field below
    justification = 0  # left/top justified
    value_bytes = initial_value.encode("ascii")
    assert len(value_bytes) == length, "initial_value must be pre-padded to exactly `length` bytes"
    body = (
        u16(width)
        + u16(height)
        + bytes([background_colour])
        + u16(font_id)
        + u16(input_attributes)
        + bytes([options])
        + u16(variable_reference)
        + bytes([justification])
        + bytes([length])
        + value_bytes
        + bytes([enabled])
        + macro_list()
    )
    return object_header(object_id, T_INPUT_STRING) + body


def make_output_rectangle(object_id, width, height, fill_attr_id):
    line_suppression = 0  # draw all four sides
    body = (
        u16(ID_LINE_ATTR)
        + u16(width)
        + u16(height)
        + bytes([line_suppression])
        + u16(fill_attr_id)
        + macro_list()
    )
    return object_header(object_id, T_OUTPUT_RECTANGLE) + body


def make_soft_key_mask(key_ids, mask_id=ID_SOFT_KEY_MASK):
    # Macro count is a header field (0 below); no trailing macro byte.
    background_colour = COLOUR_WHITE
    body = (
        bytes([background_colour])
        + bytes([len(key_ids), 0])  # childrenToFollow, macrosToFollow
        + b"".join(u16(k) for k in key_ids)
    )
    return object_header(mask_id, T_SOFT_KEY_MASK) + body


def make_key(object_id, key_code, children):
    # Macro count is a header field (0 below); no trailing macro byte.
    background_colour = COLOUR_WHITE
    body = (
        bytes([background_colour, key_code])
        + bytes([len(children), 0])
        + b"".join(child_ref(oid, x, y) for oid, x, y in children)
    )
    return object_header(object_id, T_KEY) + body


def make_auxiliary_function_type2(object_id, function_type, children):
    # Not a child of the Working Set or any mask -- per
    # WorkingSet::get_is_valid() in AgIsoStack++, Auxiliary Function
    # objects are deliberately NOT among the object types a Working Set
    # may list as a child; they just need to exist as independent
    # top-level objects in the pool. No macro support for this object type
    # (no macro count field at all, unlike most other objects).
    background_colour = COLOUR_WHITE
    function_type_byte = function_type & 0x1F  # bits 5-7: Critical/Assignment flags, all 0
    body = (
        bytes([background_colour, function_type_byte])
        + bytes([len(children)])
        + b"".join(child_ref(oid, x, y) for oid, x, y in children)
    )
    return object_header(object_id, T_AUXILIARY_FUNCTION_TYPE_2) + body


def make_font_attributes(object_id, size=1, font_type=0, style=0, colour=COLOUR_BLACK):
    body = bytes([colour, size, font_type, style]) + macro_list()
    return object_header(object_id, T_FONT_ATTRIBUTES) + body


def make_line_attributes(object_id, colour=COLOUR_BLACK, width=1, pattern=0xFFFF):
    body = bytes([colour, width]) + u16(pattern) + macro_list()
    return object_header(object_id, T_LINE_ATTRIBUTES) + body


def make_fill_attributes(object_id, fill_type=0, colour=COLOUR_BLACK, pattern=NULL_OBJECT_ID):
    body = bytes([fill_type, colour]) + u16(pattern) + macro_list()
    return object_header(object_id, T_FILL_ATTRIBUTES) + body


def build_pool():
    objects = []

    # --- Shared attribute objects ---
    objects.append(make_font_attributes(ID_FONT, size=1))  # 8x8 -- title only
    objects.append(make_font_attributes(ID_FONT_LARGE, size=7))  # 32x32
    objects.append(make_font_attributes(ID_FONT_LARGE_UNDERLINE, size=7, style=FONT_STYLE_UNDERLINED))
    objects.append(make_line_attributes(ID_LINE_ATTR, colour=COLOUR_BLACK, width=1))

    # --- Data Mask contents: title + relay indicators, 4-per-row x 2 rows
    # (60x60 boxes with a 32x32 label under each -- both bumped up from an
    # earlier 32x32/8x8 pass that turned out to be barely readable), plus a
    # small digital-input indicator box under each one (Phase 6: DI{n} acts
    # as a limit-switch interlock for channel {n}, so seeing the raw input
    # state next to its channel is the point, not just bring-up/testing).
    RECT_SIZE = 60
    LABEL_HEIGHT = 36
    # Wider than RECT_SIZE: "R1" fit in 60px, but "R1!" (the disabled
    # marker -- see below) didn't. Columns spaced to match, using free
    # screen space that was going unused rather than shrinking the font.
    LABEL_WIDTH = 100
    DI_SIZE = 20
    COLUMNS = 4
    COL_SPACING = LABEL_WIDTH + 10
    ROW_SPACING = RECT_SIZE + LABEL_HEIGHT + DI_SIZE + 24
    LEFT_MARGIN = 8
    TOP_MARGIN = 20
    # TITLE_MAX_CHARS is a module-level constant (see its definition near
    # ID_TITLE_STRING): firmware overwrites the title at connect time
    # (send_change_string_value) with "AgIsoRelayBlock <build version>"
    # once esp_app_get_description() is available, so build/deploy
    # mismatches are visible on the VT itself instead of only in a serial
    # log. Sized for a git-describe-style version string
    # ("v0.3.0-2-gfe1ee66-dirty") with room to spare, while staying well
    # under the ~448px the relay grid already occupies (LEFT_MARGIN +
    # COLUMNS * COL_SPACING).

    data_mask_children = [(ID_TITLE_STRING, LEFT_MARGIN, 4)]
    objects.append(make_output_string(ID_TITLE_STRING, TITLE_MAX_CHARS * 8, 12,
                                       "AgIsoRelayBlock".ljust(TITLE_MAX_CHARS)))

    for ch in range(1, 9):
        col = (ch - 1) % COLUMNS
        row = (ch - 1) // COLUMNS
        x = LEFT_MARGIN + col * COL_SPACING
        rect_y = TOP_MARGIN + row * ROW_SPACING
        label_y = rect_y + RECT_SIZE + 4
        di_y = label_y + LABEL_HEIGHT + 6

        fill_id = relay_fill_attr_id(ch)
        rect_id = relay_rect_id(ch)
        label_id = relay_label_id(ch)

        # Safe default (N4): every relay indicator starts unfilled (off).
        objects.append(make_fill_attributes(fill_id, fill_type=0, colour=COLOUR_BLACK))
        objects.append(make_output_rectangle(rect_id, RECT_SIZE, RECT_SIZE, fill_id))
        # Label stays below (not inside) the rectangle: black-on-black text
        # would vanish when the indicator fills solid for the ON state.
        objects.append(make_output_string(label_id, LABEL_WIDTH, LABEL_HEIGHT,
                                          "R{}".format(ch), font_id=ID_FONT_LARGE))

        # Digital input state indicator: small square, unfilled = inactive,
        # filled = active. No label needed -- position under the matching
        # channel already says what it is.
        di_fill_id = di_fill_attr_id(ch)
        di_id = di_rect_id(ch)
        objects.append(make_fill_attributes(di_fill_id, fill_type=0, colour=COLOUR_BLACK))
        objects.append(make_output_rectangle(di_id, DI_SIZE, DI_SIZE, di_fill_id))

        data_mask_children.append((rect_id, x, rect_y))
        data_mask_children.append((label_id, x, label_y))
        data_mask_children.append((di_id, x, di_y))

    # --- "Momentary Override Safety" checkbox, below the relay grid.
    # Unfilled (default/safe) = a momentary AUX-N/SKM press is refused like
    # any other control path while the channel's DI interlock has it
    # disabled. Filled = that one path (only that one -- toggle SKM/AUX-N
    # still always refused) is allowed to turn the channel back on anyway,
    # e.g. to nudge an actuator past a limit switch on purpose. See
    # docs/vt-ui-design.md for the full rationale/example.
    CHECKBOX_SIZE = 24
    override_y = TOP_MARGIN + 2 * ROW_SPACING + 20
    objects.append(make_fill_attributes(ID_OVERRIDE_CHECKBOX_FILL, fill_type=0, colour=COLOUR_BLACK))
    objects.append(make_output_rectangle(ID_OVERRIDE_CHECKBOX_RECT, CHECKBOX_SIZE, CHECKBOX_SIZE,
                                          ID_OVERRIDE_CHECKBOX_FILL))
    objects.append(make_output_string(ID_OVERRIDE_CHECKBOX_LABEL, 220, 12,
                                       "Momentary Override Safety", font_id=ID_FONT))
    data_mask_children.append((ID_OVERRIDE_CHECKBOX_RECT, LEFT_MARGIN, override_y))
    data_mask_children.append((ID_OVERRIDE_CHECKBOX_LABEL, LEFT_MARGIN + CHECKBOX_SIZE + 8, override_y + 6))

    # --- WiFi status/control panel, below the override checkbox. Reached
    # via SKM page 3 (SK1 there toggles the AP on/off), but the display
    # itself -- like the override checkbox above -- lives on the Data Mask
    # so it's visible regardless of which SKM page is active, not just
    # while on page 3. Values (SSID/IP/client count) are placeholders here,
    # sent for real via send_change_string_value once net::wifi_ap is up
    # (the pool is generated before that's known); the password Input
    # String's placeholder is never actually shown -- it's overwritten with
    # the real generated/persisted password the same way. See
    # docs/vt-ui-design.md#wifi-status--control-panel.
    wifi_y = override_y + 40
    LINE_HEIGHT = 18
    objects.append(make_fill_attributes(ID_WIFI_ENABLED_FILL, fill_type=0, colour=COLOUR_BLACK))
    objects.append(make_output_rectangle(ID_WIFI_ENABLED_RECT, CHECKBOX_SIZE, CHECKBOX_SIZE, ID_WIFI_ENABLED_FILL))
    objects.append(make_output_string(ID_WIFI_ENABLED_LABEL, 200, 12, "WiFi AP Enabled", font_id=ID_FONT))
    objects.append(make_output_string(ID_WIFI_SSID_LABEL, 256, 12, "SSID: ".ljust(32), font_id=ID_FONT))
    objects.append(make_input_string(ID_WIFI_PASSWORD_INPUT, 256, 12, WIFI_PASSWORD_MAX_CHARS,
                                      "Password: ".ljust(WIFI_PASSWORD_MAX_CHARS), font_id=ID_FONT))
    objects.append(make_output_string(ID_WIFI_IP_LABEL, 192, 12, "IP: ".ljust(24), font_id=ID_FONT))
    objects.append(make_output_string(ID_WIFI_CLIENTS_LABEL, 128, 12, "Clients: ".ljust(16), font_id=ID_FONT))
    data_mask_children.append((ID_WIFI_ENABLED_RECT, LEFT_MARGIN, wifi_y))
    data_mask_children.append((ID_WIFI_ENABLED_LABEL, LEFT_MARGIN + CHECKBOX_SIZE + 8, wifi_y + 6))
    data_mask_children.append((ID_WIFI_SSID_LABEL, LEFT_MARGIN, wifi_y + CHECKBOX_SIZE + LINE_HEIGHT * 0))
    data_mask_children.append((ID_WIFI_PASSWORD_INPUT, LEFT_MARGIN, wifi_y + CHECKBOX_SIZE + LINE_HEIGHT * 1))
    data_mask_children.append((ID_WIFI_IP_LABEL, LEFT_MARGIN, wifi_y + CHECKBOX_SIZE + LINE_HEIGHT * 2))
    data_mask_children.append((ID_WIFI_CLIENTS_LABEL, LEFT_MARGIN, wifi_y + CHECKBOX_SIZE + LINE_HEIGHT * 3))

    # --- Soft Key Mask page 1 (default/initial): SK1-SK8 (relay toggles) +
    # SK9 (buzzer) + SK10 (next page). Emitted *before* the Data Mask that
    # references it, and Key objects before the mask that references them:
    # every object here is defined before anything that points to its ID.
    # AgIsoStack++'s own parser doesn't care about forward references (it
    # parses the whole pool into a map before resolving anything), but
    # there's no reason to rely on that leniency when a strictly bottom-up
    # order costs nothing.
    key_ids = []
    for k in range(1, 11):
        key_id = softkey_id(k)
        label_id = softkey_label_id(k)
        # SK1-SK8 toggle the relay (matches the AUX-N "toggle" variant's
        # behavior), so they get the same "R{n}" text + underline
        # convention: underlined = toggles/latches, plain = hold-to-run.
        # SK9 (buzzer) and SK10 (page nav) have no such distinction to
        # make, so no underline.
        if k == 9:
            label_text, font_id = "BZ", ID_FONT_LARGE
        elif k == 10:
            label_text, font_id = ">>", ID_FONT_LARGE
        else:
            label_text, font_id = "R{}".format(k), ID_FONT_LARGE_UNDERLINE

        objects.append(make_output_string(label_id, RECT_SIZE, LABEL_HEIGHT, label_text, font_id=font_id))
        objects.append(make_key(key_id, key_code=k, children=[(label_id, 2, 2)]))
        key_ids.append(key_id)

    objects.append(make_soft_key_mask(key_ids))

    # --- Soft Key Mask page 2: SK1-SK8 momentary-override + a back key.
    # Reuses each channel's own Data Mask "R{n}" label (plain, no
    # underline -- same convention as the AUX-N momentary variant) rather
    # than creating duplicate label objects.
    page2_key_ids = []
    for ch in range(1, 9):
        key_id = softkey2_id(ch)
        objects.append(make_key(key_id, key_code=ch, children=[(relay_label_id(ch), 2, 2)]))
        page2_key_ids.append(key_id)

    objects.append(make_output_string(ID_SOFTKEY_BACK_LABEL, RECT_SIZE, LABEL_HEIGHT, "<<", font_id=ID_FONT_LARGE))
    objects.append(make_key(ID_SOFTKEY_BACK, key_code=9, children=[(ID_SOFTKEY_BACK_LABEL, 2, 2)]))
    page2_key_ids.append(ID_SOFTKEY_BACK)

    # SK10 here reuses page 1's own ">>" label object (identical meaning:
    # go further) rather than defining a duplicate -- an object may be the
    # child of more than one parent, same trick already used for the title
    # string and the page-2 momentary keys' "R{n}" labels.
    objects.append(make_key(ID_SOFTKEY_NEXT_3, key_code=10, children=[(softkey_label_id(10), 2, 2)]))
    page2_key_ids.append(ID_SOFTKEY_NEXT_3)

    objects.append(make_soft_key_mask(page2_key_ids, mask_id=ID_SOFT_KEY_MASK_2))

    # --- Soft Key Mask page 3: WiFi status/control panel (see the Data
    # Mask objects above) -- SK1 toggles the "WiFi AP Enabled" checkbox,
    # SK2 toggles "Momentary Override Safety" (moved here from page 2's
    # SK10 -- a device-wide setting fits more naturally alongside other
    # device-wide settings than next to per-channel momentary keys), SK9
    # goes back to page 2 (reuses page 2's own "<<" label object, same
    # reuse trick as above).
    objects.append(make_output_string(ID_SOFTKEY_WIFI_TOGGLE_LABEL, RECT_SIZE, LABEL_HEIGHT, "AP", font_id=ID_FONT_LARGE))
    objects.append(make_key(ID_SOFTKEY_WIFI_TOGGLE, key_code=1, children=[(ID_SOFTKEY_WIFI_TOGGLE_LABEL, 2, 2)]))

    objects.append(make_output_string(ID_SOFTKEY_OVERRIDE_LABEL, RECT_SIZE, LABEL_HEIGHT, "OR", font_id=ID_FONT_LARGE))
    objects.append(make_key(ID_SOFTKEY_OVERRIDE, key_code=2, children=[(ID_SOFTKEY_OVERRIDE_LABEL, 2, 2)]))

    objects.append(make_key(ID_SOFTKEY_BACK_3, key_code=9, children=[(ID_SOFTKEY_BACK_LABEL, 2, 2)]))

    objects.append(make_soft_key_mask(
        [ID_SOFTKEY_WIFI_TOGGLE, ID_SOFTKEY_OVERRIDE, ID_SOFTKEY_BACK_3], mask_id=ID_SOFT_KEY_MASK_3))

    objects.append(make_data_mask(data_mask_children))

    # --- Auxiliary Function Type 2 objects: AUX-N joystick/armrest
    # assignment. Two variants per relay channel, BOTH declared as
    # non-latching/momentary FunctionType -- most tractors only expose
    # momentary (spring-return) physical buttons on the joystick/armrest,
    # and a tractor's own AUX-N assignment menu generally only offers
    # inputs and functions of matching type, so declaring one variant as
    # "latching" risked it not even being assignable to a real button (or
    # working inconsistently across tractors that *are* lenient about it).
    # Instead, the "latching" (toggle-and-stay) *result* the operator wants
    # is produced by our own firmware: one variant mirrors the input value
    # straight to the relay (hold-to-run), the other toggles the relay on
    # each rising edge of an otherwise-identical momentary input (see
    # handle_aux_function_event() in vt_app.cpp). Not children of anything
    # -- see make_auxiliary_function_type2()'s docstring.
    for ch in range(1, 9):
        # "Toggle" variant: same "R{ch}" text as the hold-to-run variant
        # (an earlier "R{ch}#" pass rendered as a clipped, unlabeled "R" in
        # the AUX-N assignment list -- its label object had never been
        # sized up in the same pass that fixed every *other* label's size,
        # so it was still 16x10 in the small 8x8 font), distinguished
        # instead by an underlined font so it doesn't depend on box size to
        # read correctly. Both are declared non-latching at the protocol
        # level; only our own handling of the toggle variant differs.
        latch_label_id = aux_latch_label_id(ch)
        objects.append(make_output_string(latch_label_id, RECT_SIZE, LABEL_HEIGHT,
                                          "R{}".format(ch), font_id=ID_FONT_LARGE_UNDERLINE))
        objects.append(make_auxiliary_function_type2(
            aux_latch_function_id(ch), AUX_FUNC_NON_LATCHING_MOMENTARY,
            children=[(latch_label_id, 2, 2)]))

        objects.append(make_auxiliary_function_type2(
            aux_momentary_function_id(ch), AUX_FUNC_NON_LATCHING_MOMENTARY,
            children=[(relay_label_id(ch), 2, 2)]))

    # Buzzer: its own dedicated "BZ" label, sized the same generous way as
    # every other label here (the box, not text length, was what caused
    # the earlier clipping bug -- see the toggle variant's history above).
    objects.append(make_output_string(ID_AUX_BUZZER_LABEL, RECT_SIZE, LABEL_HEIGHT,
                                      "BZ", font_id=ID_FONT_LARGE))
    objects.append(make_auxiliary_function_type2(
        ID_AUX_BUZZER_FUNCTION, AUX_FUNC_NON_LATCHING_MOMENTARY,
        children=[(ID_AUX_BUZZER_LABEL, 2, 2)]))

    # --- Working Set (root) ---
    # Reuses the title string as its designator (valid: an object may be
    # the child of more than one parent).
    objects.append(make_working_set(ID_DATA_MASK, [(ID_TITLE_STRING, 0, 0)]))

    return b"".join(objects)


def generate_ids_header():
    lines = [
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "// Auto-generated by firmware/tools/gen_object_pool.py -- do not hand-edit.",
        "// Object ID constants matching object_pool.iop. See docs/vt-ui-design.md.",
        "",
        "namespace iso::object_pool_ids {",
        "",
        "constexpr uint16_t kWorkingSet = {};".format(ID_WORKING_SET),
        "constexpr uint16_t kDataMask = {};".format(ID_DATA_MASK),
        "constexpr uint16_t kSoftKeyMask = {};".format(ID_SOFT_KEY_MASK),
        "constexpr uint16_t kSoftKeyMask2 = {};".format(ID_SOFT_KEY_MASK_2),
        "constexpr uint16_t kSoftKeyMask3 = {};".format(ID_SOFT_KEY_MASK_3),
        "constexpr uint16_t kTitleString = {};".format(ID_TITLE_STRING),
        "constexpr uint16_t kTitleStringMaxChars = {};".format(TITLE_MAX_CHARS),
        "",
        "constexpr uint16_t kSoftkeyNext3 = {};".format(ID_SOFTKEY_NEXT_3),
        "constexpr uint16_t kSoftkeyBack3 = {};".format(ID_SOFTKEY_BACK_3),
        "",
        "// WiFi status/control panel (page 3) -- see net/wifi_ap.hpp.",
        "constexpr uint16_t kSoftkeyWifiToggle = {};".format(ID_SOFTKEY_WIFI_TOGGLE),
        "constexpr uint16_t kWifiEnabledFillAttr = {};".format(ID_WIFI_ENABLED_FILL),
        "constexpr uint16_t kWifiSsidLabel = {};".format(ID_WIFI_SSID_LABEL),
        "constexpr uint16_t kWifiPasswordInput = {};".format(ID_WIFI_PASSWORD_INPUT),
        "constexpr uint16_t kWifiIpLabel = {};".format(ID_WIFI_IP_LABEL),
        "constexpr uint16_t kWifiClientsLabel = {};".format(ID_WIFI_CLIENTS_LABEL),
        "constexpr uint16_t kWifiPasswordMaxChars = {};".format(WIFI_PASSWORD_MAX_CHARS),
        "",
        "// channel: 1-8",
        "inline uint16_t relay_rect_id(int channel) {{ return {} + channel; }}".format(1110),
        "inline uint16_t relay_fill_attr_id(int channel) {{ return {} + channel; }}".format(1920),
        "inline uint16_t relay_label_id(int channel) {{ return {} + channel; }}".format(1120),
        "inline uint16_t di_fill_attr_id(int channel) {{ return {} + channel; }}".format(1930),
        "",
        "// key_number: 1-8 = relay channels (toggle), 9 = buzzer, 10 = next page",
        "inline uint16_t softkey_id(int key_number) {{ return {} + key_number; }}".format(1210),
        "",
        "// Soft Key Mask page 2: channel 1-8 = momentary override, plus a back key.",
        "inline uint16_t softkey2_id(int channel) {{ return {} + channel; }}".format(1250),
        "constexpr uint16_t kSoftkeyBack = {};".format(ID_SOFTKEY_BACK),
        "constexpr uint16_t kSoftkeyOverrideToggle = {};".format(ID_SOFTKEY_OVERRIDE),
        "constexpr uint16_t kOverrideCheckboxFillAttr = {};".format(ID_OVERRIDE_CHECKBOX_FILL),
        "",
        "// AUX-N Auxiliary Function Type 2 objects. channel: 1-8.",
        "inline uint16_t aux_latch_function_id(int channel) {{ return {} + channel; }}".format(1500),
        "inline uint16_t aux_momentary_function_id(int channel) {{ return {} + channel; }}".format(1520),
        "constexpr uint16_t kAuxBuzzerFunction = {};".format(ID_AUX_BUZZER_FUNCTION),
        "",
        "}  // namespace iso::object_pool_ids",
        "",
    ]
    return "\n".join(lines)


def main():
    if len(sys.argv) != 3:
        print("Usage: {} <output.iop> <output_ids.hpp>".format(sys.argv[0]), file=sys.stderr)
        return 1

    pool_bytes = build_pool()
    with open(sys.argv[1], "wb") as f:
        f.write(pool_bytes)
    with open(sys.argv[2], "w") as f:
        f.write(generate_ids_header())

    print("Wrote {} bytes to {}".format(len(pool_bytes), sys.argv[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
