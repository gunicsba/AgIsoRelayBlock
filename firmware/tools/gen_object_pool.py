#!/usr/bin/env python3
"""Generates the ISO 11783-6 VT object pool binary (.iop) for the main
screen, per docs/vt-ui-design.md: a Data Mask with an 8-in-a-row relay
state indicator strip, a Main Soft Key Mask with SK1-SK8 (relay toggles) +
SK9 (buzzer pulse), and 17 Auxiliary Function Type 2 objects (a latching +
a momentary variant per relay channel, plus one momentary buzzer function)
for AUX-N joystick/armrest assignment.

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
ID_SOFT_KEY_MASK = 1200
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


def aux_latch_function_id(channel):  # channel: 1-8
    return 1500 + channel


def aux_momentary_function_id(channel):  # channel: 1-8
    return 1520 + channel


def aux_latch_label_id(channel):
    return 1540 + channel


ID_AUX_BUZZER_FUNCTION = 1560
ID_AUX_BUZZER_LABEL = 1561


def softkey_id(key_number):  # key_number: 1-9 (1-8 relays, 9 buzzer)
    return 1210 + key_number


def softkey_label_id(key_number):
    return 1230 + key_number


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


def make_soft_key_mask(key_ids):
    # Macro count is a header field (0 below); no trailing macro byte.
    background_colour = COLOUR_WHITE
    body = (
        bytes([background_colour])
        + bytes([len(key_ids), 0])  # childrenToFollow, macrosToFollow
        + b"".join(u16(k) for k in key_ids)
    )
    return object_header(ID_SOFT_KEY_MASK, T_SOFT_KEY_MASK) + body


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
    # earlier 32x32/8x8 pass that turned out to be barely readable).
    RECT_SIZE = 60
    LABEL_HEIGHT = 36
    COLUMNS = 4
    COL_SPACING = RECT_SIZE + 10
    ROW_SPACING = RECT_SIZE + LABEL_HEIGHT + 14
    LEFT_MARGIN = 8
    TOP_MARGIN = 20

    data_mask_children = [(ID_TITLE_STRING, LEFT_MARGIN, 4)]
    objects.append(make_output_string(ID_TITLE_STRING, 160, 12, "AgIsoRelayBlock"))

    for ch in range(1, 9):
        col = (ch - 1) % COLUMNS
        row = (ch - 1) // COLUMNS
        x = LEFT_MARGIN + col * COL_SPACING
        rect_y = TOP_MARGIN + row * ROW_SPACING
        label_y = rect_y + RECT_SIZE + 4

        fill_id = relay_fill_attr_id(ch)
        rect_id = relay_rect_id(ch)
        label_id = relay_label_id(ch)

        # Safe default (N4): every relay indicator starts unfilled (off).
        objects.append(make_fill_attributes(fill_id, fill_type=0, colour=COLOUR_BLACK))
        objects.append(make_output_rectangle(rect_id, RECT_SIZE, RECT_SIZE, fill_id))
        # Label stays below (not inside) the rectangle: black-on-black text
        # would vanish when the indicator fills solid for the ON state.
        objects.append(make_output_string(label_id, RECT_SIZE, LABEL_HEIGHT,
                                          "R{}".format(ch), font_id=ID_FONT_LARGE))

        data_mask_children.append((rect_id, x, rect_y))
        data_mask_children.append((label_id, x, label_y))

    # --- Main Soft Key Mask: SK1-SK8 (relay toggles) + SK9 (buzzer) ---
    # Emitted *before* the Data Mask that references it, and Key objects
    # before the mask that references them: every object here is defined
    # before anything that points to its ID. AgIsoStack++'s own parser
    # doesn't care about forward references (it parses the whole pool into
    # a map before resolving anything), but there's no reason to rely on
    # that leniency when a strictly bottom-up order costs nothing.
    key_ids = []
    for k in range(1, 10):
        key_id = softkey_id(k)
        label_id = softkey_label_id(k)
        # SK1-SK8 toggle the relay (matches the AUX-N "toggle" variant's
        # behavior), so they get the same "R{n}" text + underline
        # convention: underlined = toggles/latches, plain = hold-to-run.
        # SK9 (buzzer) has no such distinction to make, so no underline.
        if k == 9:
            label_text, font_id = "BZ", ID_FONT_LARGE
        else:
            label_text, font_id = "R{}".format(k), ID_FONT_LARGE_UNDERLINE

        objects.append(make_output_string(label_id, RECT_SIZE, LABEL_HEIGHT, label_text, font_id=font_id))
        objects.append(make_key(key_id, key_code=k, children=[(label_id, 2, 2)]))
        key_ids.append(key_id)

    objects.append(make_soft_key_mask(key_ids))
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
        "",
        "// channel: 1-8",
        "inline uint16_t relay_rect_id(int channel) {{ return {} + channel; }}".format(1110),
        "inline uint16_t relay_fill_attr_id(int channel) {{ return {} + channel; }}".format(1920),
        "",
        "// key_number: 1-8 = relay channels, 9 = buzzer",
        "inline uint16_t softkey_id(int key_number) {{ return {} + key_number; }}".format(1210),
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
