"""M2 audible pass helper: read a field's current value, optionally write a new one,
then read back to confirm the round trip. No interactive prompts (the audible
confirmation happens in chat with the user, not via stdin) -- safe to run
non-interactively from a script or tool call.

SAFETY (docs/BRIEF.md §5): only run against a scratch project.

Usage:
  python write_field.py --address "30 00 20 32" --size 4                  (read only)
  python write_field.py --address "30 00 20 32" --size 4 --write "00 00 03 FF"
"""

import argparse
import os
import sys

import mc707


def read_field(inport, outport, address, size, timeout):
    reply = mc707.send_and_wait(inport, outport, mc707.build_rq1_data(address, (0, 0, 0, size)), timeout)
    if reply is None:
        print("FAIL: no reply to RQ1 read.")
        return None
    try:
        parsed = mc707.parse_dt1_message(reply)
    except ValueError as e:
        print(f"FAIL: reply did not parse as DT1: {e}")
        return None
    return parsed["data"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True, help="4-byte address in hex, e.g. '30 00 20 32'")
    parser.add_argument("--size", type=int, required=True, help="field width in bytes")
    parser.add_argument("--write", help="hex bytes to write (same width as --size), e.g. '00 00 03 FF'")
    parser.add_argument("--port-name", default="MC-707")
    parser.add_argument("--timeout", type=float, default=2.0)
    args = parser.parse_args()

    address = mc707.parse_hex_bytes(args.address)
    if len(address) != 4:
        print("FAIL: --address must be exactly 4 bytes.")
        sys.stdout.flush()
        os._exit(1)

    inport, outport = mc707.open_ports(args.port_name)
    try:
        before = read_field(inport, outport, address, args.size, args.timeout)
        if before is None:
            sys.stdout.flush()
            os._exit(1)
        print(f"BEFORE: {' '.join(f'{b:02X}' for b in before)}")

        if args.write:
            data = mc707.parse_hex_bytes(args.write)
            if len(data) != args.size:
                print(f"FAIL: --write must be exactly {args.size} bytes.")
                sys.stdout.flush()
                os._exit(1)
            mc707.send_and_wait(inport, outport, mc707.build_dt1_data(address, data), args.timeout)
            after = read_field(inport, outport, address, args.size, args.timeout)
            if after is None:
                sys.stdout.flush()
                os._exit(1)
            print(f"WROTE:  {' '.join(f'{b:02X}' for b in data)}")
            print(f"AFTER:  {' '.join(f'{b:02X}' for b in after)}")
            print("ROUND TRIP OK" if bytes(after) == bytes(data) else "ROUND TRIP MISMATCH")
    finally:
        mc707.safe_close(inport, outport)


if __name__ == "__main__":
    main()
    sys.stdout.flush()
    os._exit(0)
