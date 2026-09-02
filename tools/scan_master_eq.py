"""One-off scan of the 0x10000000-0x10004000 "settings" region (per
Locriana/mctools_basic's own hack_tools.py::dump_settings()) for the exact
Master EQ signature read live off the device's TOTAL EFFECTS > EQ screen:
  +0x00 Input Gain   = 79  (+15dB)
  +0x01 Low Gain     = 64  (0dB)
  +0x02 Mid1 Gain    = 64  (0dB)
  +0x04 Mid3 Gain    = 70  (+6dB)
  +0x05 High Gain    = 67  (+3dB)
  +0x06 Low Freq     = 10  (200Hz, index into the doc's fixed frequency table)
  +0x09 Mid3 Freq    = 17  (1000Hz)
  +0x0A High Freq    = 24  (5000Hz)
  +0x0D Mid3 Q       = 1   (1.0)
Offsets +3 (Mid2 Gain), +7/+8 (Mid1/Mid2 Freq), +11/+12 (Mid1/Mid2 Q), +14
(Switch) are unknown and not checked. 9 known bytes at fixed relative
positions is specific enough to all but rule out a false-positive match.

Read-only.
"""

import os
import sys

import mc707
from zencore_addressing import add_7bit_carry

SIGNATURE = {0: 79, 1: 64, 2: 64, 4: 70, 5: 67, 6: 10, 9: 17, 10: 24, 13: 1}
WINDOW = max(SIGNATURE) + 1


def main():
    inport, outport = mc707.open_ports("MC-707")
    chunk_size = 16
    start = 0x10000000
    steps = 0x400  # 0x400 * 16 = 0x4000 bytes total

    all_bytes = {}

    try:
        addr = start
        for step in range(steps):
            size_bytes = (0, 0, 0, chunk_size)
            addr_bytes = ((addr >> 24) & 0xFF, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF)
            reply = mc707.send_and_wait(inport, outport, mc707.build_rq1_data(addr_bytes, size_bytes), timeout=0.3)
            if reply is not None:
                try:
                    parsed = mc707.parse_dt1_message(reply)
                    for i, b in enumerate(parsed["data"]):
                        all_bytes[addr + i] = b
                except ValueError:
                    pass
            addr = add_7bit_carry(addr, chunk_size)
            if step % 64 == 0:
                print(f"  ...step {step}/{steps}, now at 0x{addr:08X}, {len(all_bytes)} bytes so far", file=sys.stderr)
    finally:
        mc707.safe_close(inport, outport)

    print(f"Collected {len(all_bytes)} bytes.")

    sorted_addrs = sorted(all_bytes.keys())
    found = 0
    for a in sorted_addrs:
        window = [all_bytes.get(a + i) for i in range(WINDOW)]
        if any(window[off] != val for off, val in SIGNATURE.items()):
            continue
        print(f"CANDIDATE Master EQ at 0x{a:08X}: {window}")
        found += 1

    print(f"\n{found} candidate(s) found.")


if __name__ == "__main__":
    main()
    sys.stdout.flush()
    os._exit(0)
