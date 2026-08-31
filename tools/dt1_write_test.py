"""M1 steps 3 & 4: DT1-write a parameter, ask whether it was audible, then RQ1
re-read the same address to confirm the round trip.

SAFETY (docs/BRIEF.md §5): only ever run this against a scratch project you don't
care about. A bad write could corrupt a project; this path is undocumented/unofficial.

Usage:
  python dt1_write_test.py --replay-known
  python dt1_write_test.py --address "30 00 00 00" --data "7F"
"""

import argparse
import os
import sys

import mc707

# Known-working example from Locriana/mcpoker, cited in docs/BRIEF.md §3.
KNOWN_ADDRESS = (0x30, 0x42, 0x00, 0x18)
KNOWN_DATA = (0x42,)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", help="4-byte address in hex, e.g. '30 00 00 00'")
    parser.add_argument("--data", help="1+ byte payload in hex, e.g. '7F'")
    parser.add_argument("--replay-known", action="store_true",
                         help="ignore --address/--data and replay the known-working mcpoker example")
    parser.add_argument("--port-name", default="MC-707")
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--yes", action="store_true", help="skip the confirmation prompt before writing")
    args = parser.parse_args()

    print("=" * 70)
    print("SAFETY: only run this against a scratch project. See docs/BRIEF.md §5.")
    print("=" * 70)

    if args.replay_known:
        address, data = KNOWN_ADDRESS, KNOWN_DATA
    else:
        if not args.address or not args.data:
            print("FAIL: pass --replay-known, or both --address and --data.")
            sys.stdout.flush()
            os._exit(1)
        address = mc707.parse_hex_bytes(args.address)
        data = mc707.parse_hex_bytes(args.data)
        if len(address) != 4:
            print("FAIL: --address must be exactly 4 bytes.")
            sys.stdout.flush()
            os._exit(1)

    addr_hex = " ".join(f"{b:02X}" for b in address)
    data_hex = " ".join(f"{b:02X}" for b in data)
    print(f"About to DT1-write address [{addr_hex}] = [{data_hex}]")

    if not args.yes:
        confirm = input("Proceed? [y/N] ").strip().lower()
        if confirm != "y":
            print("Aborted.")
            sys.stdout.flush()
            os._exit(0)

    inport, outport = mc707.open_ports(args.port_name)
    try:
        write_reply = mc707.send_and_wait(inport, outport, mc707.build_dt1_data(address, data), args.timeout)
        if write_reply is not None:
            print("Note: device sent an immediate reply to the write (not required, logging it above).")

        heard = input("Did the sound audibly change? [y/n/unsure] ").strip().lower()
        print(f"Audible change reported: {heard}")

        print("Re-reading the same address to confirm the round trip...")
        read_size = (0, 0, 0, len(data))
        reply = mc707.send_and_wait(inport, outport, mc707.build_rq1_data(address, read_size), args.timeout)
    finally:
        mc707.safe_close(inport, outport)

    if reply is None:
        print("FAIL: no reply to the re-read RQ1.")
        sys.stdout.flush()
        os._exit(1)

    try:
        parsed = mc707.parse_dt1_message(reply)
    except ValueError as e:
        print(f"FAIL: re-read reply did not parse as DT1: {e}")
        sys.stdout.flush()
        os._exit(1)

    read_back = bytes(parsed["data"])
    print(f"Wrote:    {data_hex}")
    print(f"Read back: {' '.join(f'{b:02X}' for b in read_back)}")

    if read_back == bytes(data):
        print("PASS: round trip matches.")
    else:
        print("MISMATCH: read-back data does not match what was written.")


if __name__ == "__main__":
    main()
    sys.stdout.flush()
    os._exit(0)
