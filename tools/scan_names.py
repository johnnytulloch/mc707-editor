"""Scan a set of tone-slot addresses (base + n * step) and print each tone's
16-byte name field, to locate which fixed address corresponds to a tone the
user can see named on the device screen. Ad-hoc discovery helper -- see
docs/BRIEF.md §3 ("User tones are spaced 0x00020000 apart").

Usage: python scan_names.py --base 30000000 --step 00020000 --count 10
"""

import argparse
import os
import sys

import mc707


def add_addr(base, step, n):
    # Roland addresses are 7-bit-per-byte; treat as a big int and re-split,
    # valid as long as no byte overflows 0x7F.
    base_int = int.from_bytes(base, "big")
    step_int = int.from_bytes(step, "big")
    result = base_int + step_int * n
    result_bytes = result.to_bytes(4, "big")
    if any(b > 0x7F for b in result_bytes):
        return None
    return result_bytes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="30000000")
    parser.add_argument("--step", default="00020000")
    parser.add_argument("--count", type=int, default=10)
    parser.add_argument("--port-name", default="MC-707")
    parser.add_argument("--timeout", type=float, default=2.0)
    args = parser.parse_args()

    base = mc707.parse_hex_bytes(args.base)
    step = mc707.parse_hex_bytes(args.step)

    inport, outport = mc707.open_ports(args.port_name)
    try:
        for n in range(args.count):
            addr = add_addr(base, step, n)
            if addr is None:
                print(f"n={n}: address overflow, skipping")
                continue
            reply = mc707.send_and_wait(inport, outport, mc707.build_rq1_data(addr, (0, 0, 0, 16)), args.timeout)
            addr_hex = " ".join(f"{b:02X}" for b in addr)
            if reply is None:
                print(f"n={n} [{addr_hex}]: no reply")
                continue
            try:
                parsed = mc707.parse_dt1_message(reply)
                name = bytes(parsed["data"]).decode("ascii", errors="replace")
            except ValueError as e:
                print(f"n={n} [{addr_hex}]: parse error: {e}")
                continue
            print(f"n={n} [{addr_hex}]: {name!r}")
    finally:
        mc707.safe_close(inport, outport)


if __name__ == "__main__":
    main()
    sys.stdout.flush()
    os._exit(0)
