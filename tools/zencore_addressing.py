"""ZEN-Core tone-slot addressing for the MC-707 (M2 sub-problem A).

Track base-address table ported from Locriana/mcpoker's zen_core_tools.py
(public domain / MIT per docs/BRIEF.md §3, §10) -- see tools/FINDINGS.md for
the citation and the exact upstream source.

mcpoker computes a slot address as plain Python int arithmetic:
    base_address = mc_track_base[track_idx] + tone_alignment * clip_idx
That is WRONG for some (track, clip) combinations. Roland's SysEx addresses
are 7-bit clean: every one of the 4 address bytes must be 0x00-0x7F (bit 7
is reserved, same reason MIDI data bytes in general are 7-bit). When plain
hex addition pushes a byte past 0x7F, the real device address does not carry
the way a normal base-256 integer would -- it carries at 0x80 (128), i.e.
the whole 4-byte address is base-128 digits, most-significant byte first.

This was caught by cross-checking mcpoker's formula against two addresses
already confirmed live against real hardware (tools/FINDINGS.md):
  - Track 1 track-sound = 0x30200000 (confirmed by on-device name match)
  - Track 4 track-sound = 0x31060000 (confirmed by on-device name match,
    "SynthChord01 MC")
Plain hex addition reproduces the first (base 0x30000000 + 16*0x20000 =
0x30200000, no byte exceeds 0x7F) but NOT the second: mc_track_base[3] +
16*0x20000 = 0x30660000 + 0x200000 = 0x30860000 in plain hex -- byte2 = 0x86,
which is not a legal address byte. The base-128 carry gives 0x66 + 0x20 =
0x86 = 134 decimal -> 134 - 128 = 6 remainder, carry 1 into byte3 -> 0x31 06
00 00 = 0x31060000, which is exactly the confirmed address. See
add_7bit_carry() below.

Practical effect: for tracks whose base byte2 is high enough that adding the
"track sound" offset (16 * 0x20000, byte2 contribution 0x20 = 32 decimal)
or a high clip_idx offset overflows 0x7F, mcpoker's naive formula computes
the wrong address (and for the worst cases, a byte >= 0x80 that isn't even
a legal MIDI data byte -- mido would reject it outright). Affected slots,
worked out from the base table below:
  - Track 4 (idx 3, byte2 base 0x66=102): clip_idx >= 13 (clips 14-16,
    where 16 = the track-sound slot) needs the carry.
  - Track 8 (idx 7, byte2 base 0x6E=110): clip_idx >= 9 (clips 10-16,
    including track-sound) needs the carry.
All other tracks stay under 0x7F for every clip_idx 0-15 plus track-sound,
so plain addition happens to give the right answer there -- but always use
add_7bit_carry() rather than relying on which tracks are "safe".
"""

# Base addresses of the 8 tracks' first clip tone (clip index 0).
# Ported verbatim from Locriana/mctools_basic's ZenCoreTools.get_base_address.
TRACK_BASE = [
    0x30000000, 0x30220000, 0x30440000, 0x30660000,
    0x31080000, 0x312A0000, 0x314C0000, 0x316E0000,
]

CLIPS_PER_TRACK = 16

# Bytes between adjacent tone slots (also the alignment the tone structs
# live on). One track occupies CLIPS_PER_TRACK + 1 of these (16 clips, plus
# the track's own "track sound" one past the last clip).
TONE_ALIGNMENT = 0x20000


def add_7bit_carry(base, offset):
    """Adds base + offset treating both as 4-byte, 7-bit-clean (base-128)
    addresses: each byte carries into the next at 128, not 256.
    """
    b = [(base >> 24) & 0xFF, (base >> 16) & 0xFF, (base >> 8) & 0xFF, base & 0xFF]
    o = [(offset >> 24) & 0xFF, (offset >> 16) & 0xFF, (offset >> 8) & 0xFF, offset & 0xFF]

    result = [0, 0, 0, 0]
    carry = 0
    for i in (3, 2, 1, 0):
        total = b[i] + o[i] + carry
        carry = total // 0x80
        result[i] = total % 0x80

    if carry != 0:
        raise ValueError(f"address overflow: base=0x{base:08X} offset=0x{offset:08X}")

    return (result[0] << 24) | (result[1] << 16) | (result[2] << 8) | result[3]


def get_slot_address(track_idx, clip_idx=None):
    """track_idx: 0-7. clip_idx: 0-15, or None for the track's own
    "track sound" slot (the 17th slot, one past the last clip).
    """
    if not (0 <= track_idx <= 7):
        raise ValueError(f"track_idx must be 0-7, got {track_idx}")
    if clip_idx is not None and not (0 <= clip_idx <= CLIPS_PER_TRACK - 1):
        raise ValueError(f"clip_idx must be 0-15 or None, got {clip_idx}")

    n = CLIPS_PER_TRACK if clip_idx is None else clip_idx
    offset = TONE_ALIGNMENT * n
    return add_7bit_carry(TRACK_BASE[track_idx], offset)


def disp_map():
    for track_idx in range(8):
        addr = get_slot_address(track_idx)
        print(f"track {track_idx + 1} sound  @ 0x{addr:08X}")
        for clip_idx in range(CLIPS_PER_TRACK):
            addr = get_slot_address(track_idx, clip_idx)
            print(f"  track {track_idx + 1} clip {clip_idx + 1:2d} @ 0x{addr:08X}")


if __name__ == "__main__":
    disp_map()
