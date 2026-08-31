"""M1 step 2: RQ1-read a block near a given address, log the raw DT1 reply, and
surface the documented reply-address bug (docs/BRIEF.md §3 -- the reply carries the
correct payload but a wrong address field).

Usage: python rq1_probe.py --address "30 00 00 00" --size "00 00 00 10"
"""

import argparse
import os
import sys

import mc707


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True, help="4-byte address in hex, e.g. '30 00 00 00'")
    parser.add_argument("--size", default="00 00 00 10", help="4-byte size in hex, default 16 bytes")
    parser.add_argument("--port-name", default="MC-707")
    parser.add_argument("--timeout", type=float, default=2.0)
    args = parser.parse_args()

    address = mc707.parse_hex_bytes(args.address)
    size = mc707.parse_hex_bytes(args.size)
    if len(address) != 4 or len(size) != 4:
        print("FAIL: --address and --size must each be exactly 4 bytes.")
        sys.stdout.flush()
        os._exit(1)

    inport, outport = mc707.open_ports(args.port_name)
    try:
        reply = mc707.send_and_wait(inport, outport, mc707.build_rq1_data(address, size), args.timeout)
    finally:
        mc707.safe_close(inport, outport)

    if reply is None:
        print("FAIL: no reply to RQ1 (device may not honour reads at this address, or is off).")
        sys.stdout.flush()
        os._exit(1)

    try:
        parsed = mc707.parse_dt1_message(reply)
    except ValueError as e:
        print(f"FAIL: reply did not parse as a DT1 message: {e}")
        print(f"Raw reply: {' '.join(f'{b:02X}' for b in reply)}")
        sys.stdout.flush()
        os._exit(1)

    requested_hex = " ".join(f"{b:02X}" for b in address)
    reply_addr_hex = " ".join(f"{b:02X}" for b in parsed["address"])
    data_hex = " ".join(f"{b:02X}" for b in parsed["data"])

    print(f"Requested address: {requested_hex}")
    print(f"Reply address:     {reply_addr_hex}")
    if bytes(parsed["address"]) != address:
        print(">>> MISMATCH -- this is the documented reply-address bug. Use the requested")
        print(">>> address (not the reply's) when storing this data.")
    else:
        print(">>> Reply address matches the request -- bug not observed for this address.")

    print(f"Data ({len(parsed['data'])} bytes): {data_hex}")
    print(f"Checksum byte:     {parsed['checksum']:#04x}")


if __name__ == "__main__":
    main()
    sys.stdout.flush()
    os._exit(0)
