#!/usr/bin/env python3
"""Generates the ISO 11783-6 VT object pool binary (.iop) for the main
screen, per docs/vt-ui-design.md: a Data Mask with an 8-in-a-row relay
state indicator strip, and a Main Soft Key Mask with SK1-SK8 (relay
toggles) + SK9 (buzzer pulse).

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

# Standard VT colour palette indices actually used here.
COLOUR_BLACK = 0
COLOUR_WHITE = 1

# --- Object IDs -------------------------------------------------------
ID_WORKING_SET = 1000
ID_DATA_MASK = 1100
ID_TITLE_STRING = 1101
ID_SOFT_KEY_MASK = 1200
ID_FONT = 1900
ID_LINE_ATTR = 1910


def relay_rect_id(channel):  # channel: 1-8
    return 1110 + channel


def relay_label_id(channel):
    return 1120 + channel


def relay_fill_attr_id(channel):
    return 1920 + channel


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
    objects.append(make_font_attributes(ID_FONT, size=1))  # 8x8
    objects.append(make_line_attributes(ID_LINE_ATTR, colour=COLOUR_BLACK, width=1))

    # --- Data Mask contents: title + 8-in-a-row relay indicators ---
    RECT_SIZE = 32
    RECT_Y = 30
    LABEL_Y = RECT_Y + RECT_SIZE + 4
    SPACING = 40
    LEFT_MARGIN = 8

    data_mask_children = [(ID_TITLE_STRING, LEFT_MARGIN, 4)]
    objects.append(make_output_string(ID_TITLE_STRING, 160, 12, "AgIsoRelayBlock"))

    for ch in range(1, 9):
        x = LEFT_MARGIN + (ch - 1) * SPACING
        fill_id = relay_fill_attr_id(ch)
        rect_id = relay_rect_id(ch)
        label_id = relay_label_id(ch)

        # Safe default (N4): every relay indicator starts unfilled (off).
        objects.append(make_fill_attributes(fill_id, fill_type=0, colour=COLOUR_BLACK))
        objects.append(make_output_rectangle(rect_id, RECT_SIZE, RECT_SIZE, fill_id))
        objects.append(make_output_string(label_id, RECT_SIZE, 10, "R{}".format(ch)))

        data_mask_children.append((rect_id, x, RECT_Y))
        data_mask_children.append((label_id, x, LABEL_Y))

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
        label_text = "Bz" if k == 9 else str(k)

        objects.append(make_output_string(label_id, 16, 10, label_text))
        objects.append(make_key(key_id, key_code=k, children=[(label_id, 2, 2)]))
        key_ids.append(key_id)

    objects.append(make_soft_key_mask(key_ids))
    objects.append(make_data_mask(data_mask_children))

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
