# Discovery findings — M1 viability gate (2026-08-31)

Captured live against a real MC-707 on Windows, using the scripts in this directory. All four
M1 steps (brief §7) passed. Details below are the raw material for M2 (parameter map).

## M1 step 1 — Identity Request

`identity.py` sent `F0 7E 7F 06 01 F7`, got a real reply (first independent capture — brief §4
flagged this as unverified):

```
F0 7E 7F 06 02 41 5D 03 00 00 00 01 00 00 F7
```

Parsed as a standard Universal Non-Realtime Identity Reply (single-byte Roland manufacturer ID
`0x41`): family code `5D 03`, model number `00 00`, version `00 01 00 00`. The family code's first
byte, `0x5D`, matches the model ID used in the RQ1/DT1 header (`00 00 00 5D`) — these are different
fields in different message formats, not directly comparable byte-for-byte, but the `5D` showing up
in both is a strong positive signal the device is what the brief expected.

## M1 step 2 — RQ1 read + the reply-address bug

`rq1_probe.py --address "30 00 00 00" --size "00 00 00 10"` returned the current tone's name as
ASCII (`INIT TONE`, space-padded to 16 bytes) while the reply's address field claimed `06 00 00 00`
instead of the requested `30 00 00 00` — reproduces the documented reply-address bug (brief §3)
exactly, including the pattern from the brief's own MC-101 example (0x20 = ASCII space padding).

**Rule confirmed: always use the address you requested, never the reply's claimed address.**

## Tone address layout (new — not in the brief, discovered this session)

Address `0x30000000` is **not** tied to a specific track and does **not** follow front-panel track
selection — it stayed `INIT TONE` regardless. It's a fixed slot, most likely Track 1 / Clip 1.

Scanning `base=0x30000000, step=0x00020000` (the brief's documented tone spacing) for 140 slots
with `scan_names.py` found real tone names scattered through the range — most slots are empty
(unused clip tones), non-empty ones found:

| n   | Address       | Name              |
|-----|---------------|-------------------|
| 0   | `30 00 00 00` | `INIT TONE`       |
| 13  | `30 1A 00 00` | `INIT TONE`       |
| 16  | `30 20 00 00` | `5th DLA Pad`     |
| 17  | `30 22 00 00` | `INIT TONE`       |
| 32  | `30 40 00 00` | `INIT TONE`       |
| 33  | `30 42 00 00` | `Rotary Org 1`     |
| 34  | `30 44 00 00` | `INIT TONE`       |
| 50  | `30 64 00 00` | `Dark Sub`        |
| 51  | `30 66 00 00` | `INIT TONE`       |
| 131 | `31 06 00 00` | **`SynthChord01 MC`** |
| 132 | `31 08 00 00` | `INIT TONE`       |

n=64..127 all overflowed the naive `base + step*n` byte-wise addition (a mid byte would exceed
0x7F) — the real addressing scheme is not simply linear in this range; treat any arithmetic here
as heuristic, not ground truth. n=131 (`31 06 00 00`) was confirmed by name match to be **Track 4's
tone** (user confirmed on-device: Track 4 was showing `SynthChord01 MC`, and had a different name
than what `0x30000000` returned, and selecting Track 4 on the panel did not change what
`0x30000000` reports — so this is NOT a "currently selected track" pointer either; addresses
appear to be fixed per-slot regardless of front-panel state).

**Open question for M2:** the exact formula mapping (track, clip-slot) → address. `mcpoker`/
`sysex-mapatron` derivation work should resolve this properly rather than more manual scanning.

## M1 steps 3 & 4 — audible DT1 write + round trip

Within a tone's tone-common block, bytes right after the 16-byte name field (offset `+0x10`):

```
offset  +10 +11 +12 +13 +14 +15 +16 +17
INIT TONE (0x30000000): 00  00  00  00  00  64  40  00
SynthChord01 MC (0x31060000): 24  00  00  00  00  54  40  01
```

- **Offset `+0x15` = Tone Level.** Confirmed by DT1-writing `0x00` to `31 06 00 15` — the tone went
  completely silent on playback (channel 4, note 60), confirmed by user A/B listening test against
  the original value `0x54`. Round-trip re-read matched the written byte both times. Restored to
  `0x54` afterward and re-confirmed audible.
- Offset `+0x16` = `0x40` (64 decimal) in **both** tones tested — consistent with a default-center
  **Pan** value, though not independently confirmed by writing it.
- Offset `+0x10` (`0x24` vs `0x00`) differs per tone — likely a per-tone category/type byte, not
  yet identified.

## Known-working DT1 example from the brief (mcpoker)

`F0 41 10 00 00 00 5D 12 30 42 00 18 42 34 F7` (write `0x42` to `30 42 00 18`) was replayed as a
smoke test before the above — device accepted it (no error, no reply expected for DT1 writes),
round-trip re-read matched. This is inside the `n=33 "Rotary Org 1"` tone's slot (`30 42 00 00`,
offset `+0x18` from that name field) — one byte past the `+0x15/+0x16` Level/Pan pair found above,
so plausibly a third per-tone byte (Portamento switch? not tested for audibility).

## Scene change — confirmed no MIDI output (2026-08-31)

`tools/monitor.py` passively listened on the MC-707's MIDI input port (clock/active-sensing
filtered out) while the user changed Scene locally on the front panel. Tested twice: once cycling
through scenes 1→4, once repeatedly toggling between 1-1 and 1-4 over a 40s window. **Zero
messages of any kind were received in either test** — no CC, Program Change, Note, or SysEx.

This confirms Scene changes are not reflected over MIDI at all on this device/firmware, at least
not as a spontaneous transmission. It doesn't rule out a SysEx *write* address existing (RQ1/DT1
probing, like the M1 tone-parameter work, could still find one) — only that the device doesn't
announce the change itself the way it might for other state. Finding a writable address would be
its own discovery effort, structurally similar to M1 but for the pattern/scene address space
rather than the tone address space, and would cross into "pattern/clip/sequencer editing," which
`docs/BRIEF.md` §1 lists as a v1 non-goal. Not pursued further without an explicit decision to
extend scope.

**Current app behavior:** the Scene selector in the control panel remains interactive (per the
user's request) but transmits nothing — this is now a confirmed decision, not a placeholder
waiting on unverified data.

## Tooling note

`python-rtmidi`'s Windows backend can hang in `port.close()` shortly after a sysex receive.
`mc707.safe_close()` runs closes in a timeout-bounded daemon thread and every script force-exits
via `os._exit()` at the end rather than relying on normal interpreter shutdown. If you hit a hang
again, check for orphaned `python.exe` processes holding the MIDI port before re-running.
