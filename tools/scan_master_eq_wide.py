"""Second-attempt scan for the Master EQ / Total Effects address, following up on
tools/FINDINGS.md's "Total Effects" and "Factory ROM patch browsing" sections.

The first attempt (scan_master_eq.py) scanned only 0x10000000-0x10004000 (16KB) and
found nothing. New evidence motivating a wider retry: mctools_basic's own
sys_ex_comm.py has a *confirmed-working* reference address in this same family
(Track Color, 0x10000819 + 0x200 per track) -- re-verified live against this exact
device before writing this script. So 0x10000000+ is real, structured settings
memory, just possibly wider than the first 16KB tried.

Uses much bigger per-request chunks (100 bytes -- NOT 128, see the chunk_size comment
below for why -- vs. the original's 16) to cover more ground per round trip, feasible
to scan a few hundred KB instead of just 16KB in a reasonable time. Same live-confirmed
9-byte Master EQ signature as scan_master_eq.py.

RESULT (2026-09-03, live): 0 candidates across 0x10000000-0x10077F38 (~490KB of address
range swept, though only ~3.5KB of it actually replied with data -- everything past
roughly 0x10005000 was empty/non-responsive). Real, structured settings data (matches
sys_ex_comm.py's own confirmed Track Color example at 0x10000819) is clustered in a
narrow band right at the start of this region, not spread wide -- so widening the scan
past where the original attempt stopped didn't help, the ceiling was already close to
the real boundary of whatever this region actually is. Doesn't appear to be where Master
EQ lives. See tools/FINDINGS.md's "Total Effects" investigation for the fuller trail
and what's still untried.

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
    # 100, not 128: the size field is itself a 7-bit-clean SysEx byte (0-0x7F when
    # single-byte-encoded, same rule as the address field) -- 128 (0x80) is invalid
    # and gets rejected by mido before anything is even sent. Hit this live testing
    # this exact script.
    chunk_size = 100
    start = 0x10000000
    steps = 1310  # steps * chunk_size = 131000 =~ 128KB total

    all_bytes = {}
    no_reply_run = 0

    try:
        addr = start
        for step in range(steps):
            size_bytes = (0, 0, 0, chunk_size)
            addr_bytes = ((addr >> 24) & 0xFF, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF)
            reply = mc707.send_and_wait(inport, outport, mc707.build_rq1_data(addr_bytes, size_bytes), timeout=0.3)
            if reply is not None:
                no_reply_run = 0
                try:
                    parsed = mc707.parse_dt1_message(reply)
                    for i, b in enumerate(parsed["data"]):
                        all_bytes[addr + i] = b
                except ValueError:
                    pass
            else:
                no_reply_run += 1
            addr = add_7bit_carry(addr, chunk_size)
            if step % 32 == 0:
                print(f"  ...step {step}/{steps}, now at 0x{addr:08X}, {len(all_bytes)} bytes so far, "
                      f"no_reply_run={no_reply_run}", file=sys.stderr)
    finally:
        mc707.safe_close(inport, outport)

    print(f"Collected {len(all_bytes)} bytes across 0x{start:08X}-0x{addr:08X}.")

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
