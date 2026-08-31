"""Passive MIDI input monitor -- logs every incoming message (not just sysex)
with a timestamp. Used to discover what, if anything, the MC-707 transmits on
its own for actions with no documented CC/PC/SysEx (e.g. Scene changes) --
same "observe first" method as M1, just listening instead of probing.

Usage: python monitor.py [--port-name MC-707] [--seconds 20]
Ctrl+C to stop early.
"""

import argparse
import os
import sys
import time

import mido

import mc707


def format_message(msg):
    if msg.type == "sysex":
        return "SYSEX: F0 " + " ".join(f"{b:02X}" for b in msg.data) + " F7"
    return f"{msg.type.upper()}: {msg}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port-name", default="MC-707")
    parser.add_argument("--seconds", type=float, default=20.0, help="how long to listen")
    parser.add_argument("--show-clock", action="store_true", help="include clock/active_sensing spam")
    args = parser.parse_args()

    in_name, _ = mc707.find_ports(args.port_name)
    print(f"Listening on {in_name!r} for {args.seconds:.0f}s (Ctrl+C to stop early)...")
    print("Go ahead and trigger the action on the device now.")

    inport = mido.open_input(in_name)
    seen = 0
    deadline = time.time() + args.seconds
    try:
        while time.time() < deadline:
            msg = inport.poll()
            if msg is not None:
                seen += 1
                if not args.show_clock and msg.type in ("clock", "active_sensing"):
                    continue
                ts = time.strftime("%H:%M:%S")
                print(f"[{ts}] {format_message(msg)}")
            else:
                time.sleep(0.005)
    except KeyboardInterrupt:
        pass
    finally:
        mc707.safe_close(inport)

    print(f"Done. {seen} message(s) captured.")


if __name__ == "__main__":
    main()
    sys.stdout.flush()
    os._exit(0)
