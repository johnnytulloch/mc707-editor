"""M2 structural pass: RQ1-read every declared field for one tone slot, decode it per
its declared encoding, and report whether the value is in-range (docs/M2_PLAN.md's
"Structural pass (automated, every entry)").

This is read-only -- never writes. Safe to run against any project.

Usage:
  python validate_map.py --track 1 --clip 1
  python validate_map.py --track 1          (track's own "track sound", no clip)
"""

import argparse
import json
import os
import sys

import mc707
import zencore_addressing as zaddr

MAP_PATH = os.path.join(os.path.dirname(__file__), "..", "data", "zencore-map.json")


def decode(raw_bytes, encoding):
    if encoding == "ascii":
        return "".join(chr(b) if 32 <= b < 127 else "." for b in raw_bytes)
    if encoding == "uint7":
        return raw_bytes[0]
    if encoding in ("nibble2", "nibble3", "nibble4"):
        value = 0
        for b in raw_bytes:
            value = (value << 4) | (b & 0x0F)
        return value
    raise ValueError(f"unknown encoding {encoding!r}")


def add_offset(base_address, relative_offset):
    """Field offsets within a block never exceed the block size (max seen: 0x50,
    well under 0x80), and every block's own base address already has a 0x00 low
    byte, so plain addition can't overflow a byte boundary here. Slot-to-slot
    addressing (which DOES need carry-safe addition) is handled separately by
    zencore_addressing.get_slot_address().
    """
    return base_address + relative_offset


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--track", type=int, required=True, help="1-8")
    parser.add_argument("--clip", type=int, default=None, help="1-16, omit for the track's own sound")
    parser.add_argument("--block", default="tone_common", help="which block in the map to validate")
    parser.add_argument("--port-name", default="MC-707")
    parser.add_argument("--timeout", type=float, default=2.0)
    args = parser.parse_args()

    track_idx = args.track - 1
    clip_idx = None if args.clip is None else args.clip - 1
    slot_address = zaddr.get_slot_address(track_idx, clip_idx)

    with open(MAP_PATH, encoding="utf-8") as f:
        parameter_map = json.load(f)

    block = parameter_map["blocks"][args.block]
    block_base = slot_address + int(block["relative_offset"], 16) if "relative_offset" in block \
        else slot_address + int(block["relative_offset_per_partial"][0], 16)

    clip_label = f"clip {args.clip}" if args.clip is not None else "track sound"
    print(f"Validating block {args.block!r} for track {args.track} {clip_label}")
    print(f"Slot address: 0x{slot_address:08X}, block base: 0x{block_base:08X}\n")

    inport, outport = mc707.open_ports(args.port_name)
    pass_count = 0
    fail_count = 0
    try:
        for field in block["fields"]:
            field_offset = int(field["offset"], 16)
            field_address = add_offset(block_base, field_offset)
            addr_bytes = (
                (field_address >> 24) & 0xFF, (field_address >> 16) & 0xFF,
                (field_address >> 8) & 0xFF, field_address & 0xFF,
            )
            size_bytes = (0, 0, 0, field["size"])
            reply = mc707.send_and_wait(inport, outport, mc707.build_rq1_data(addr_bytes, size_bytes), args.timeout)

            if reply is None:
                print(f"  FAIL  +0x{field_offset:02X} {field['name']:<28} no reply")
                fail_count += 1
                continue

            try:
                parsed = mc707.parse_dt1_message(reply)
            except ValueError as e:
                print(f"  FAIL  +0x{field_offset:02X} {field['name']:<28} unparseable reply: {e}")
                fail_count += 1
                continue

            raw = parsed["data"]
            if len(raw) != field["size"]:
                print(f"  FAIL  +0x{field_offset:02X} {field['name']:<28} expected {field['size']} bytes, got {len(raw)}")
                fail_count += 1
                continue

            value = decode(raw, field["encoding"])
            lo, hi = field["range"]
            in_range = (isinstance(value, str)) or (lo <= value <= hi)
            status = "ok  " if in_range else "RANGE"
            print(f"  {status}  +0x{field_offset:02X} {field['name']:<28} = {value!r}"
                  f"{'' if in_range else f'  (expected {lo}-{hi})'}")
            if in_range:
                pass_count += 1
            else:
                fail_count += 1
    finally:
        mc707.safe_close(inport, outport)

    print(f"\n{pass_count} plausible, {fail_count} failed/out-of-range, {len(block['fields'])} total")


if __name__ == "__main__":
    main()
    sys.stdout.flush()
    os._exit(0)
