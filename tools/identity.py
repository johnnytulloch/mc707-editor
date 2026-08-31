"""M1 step 1: Identity Request -> confirm the MC-707's model ID.

docs/BRIEF.md §4 flags this as unverified -- "The MC-707 identity reply hex has not
been independently captured" -- so this script prints the full parsed reply rather
than silently asserting a match, and reports PASS/DIFFERENT accordingly.

Usage: python identity.py [--port-name MC-707]
"""

import argparse
import os
import sys

import mc707


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port-name", default="MC-707", help="substring to match the MIDI port name")
    parser.add_argument("--timeout", type=float, default=2.0)
    args = parser.parse_args()

    inport, outport = mc707.open_ports(args.port_name)
    try:
        reply = mc707.send_and_wait(inport, outport, mc707.build_identity_request_data(), args.timeout)
    finally:
        mc707.safe_close(inport, outport)

    if reply is None:
        print("FAIL: no Identity Reply received.")
        sys.stdout.flush()
        os._exit(1)

    try:
        parsed = mc707.parse_identity_reply(reply)
    except ValueError as e:
        print(f"FAIL: {e}")
        sys.stdout.flush()
        os._exit(1)

    print(f"Device ID:        {parsed['device_id']:#04x}")
    print(f"Manufacturer:      {' '.join(f'{b:02X}' for b in parsed['manufacturer'])}")
    print(f"Family (2B):       {' '.join(f'{b:02X}' for b in parsed['family'])}")
    print(f"Model (2B):        {' '.join(f'{b:02X}' for b in parsed['model'])}")
    print(f"Version (4B):      {' '.join(f'{b:02X}' for b in parsed['version'])}")

    got = parsed["family_and_model"]
    expected = mc707.EXPECTED_IDENTITY_MODEL
    print(f"Family+Model:      {' '.join(f'{b:02X}' for b in got)}")
    print(f"Expected (brief):  {' '.join(f'{b:02X}' for b in expected)}")

    if got == expected:
        print("PASS: model ID matches 00 00 00 5D.")
    else:
        print("DIFFERENT: reply does not literally match 00 00 00 5D in this field layout.")
        print("This does not necessarily mean the device isn't a match -- see the raw fields")
        print("above and cross-check against the RQ1/DT1 model ID used in mc707.py (also 5D).")


if __name__ == "__main__":
    main()
    sys.stdout.flush()
    os._exit(0)
