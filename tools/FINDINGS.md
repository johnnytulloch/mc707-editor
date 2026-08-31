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

## Scene change — remote control confirmed working (2026-08-31, later same day)

Reading the official Ver.1.20 update docs (while researching MIDI file import, see below) turned
up the real mechanism: system settings (MIDI tab) include **Control Channel** (CH1–16), **Control
Rx** (OFF/ON — "specifies whether scene changes are received from an external device"), and
**Control Tx MIDI OUT1/OUT2/USB MIDI** (OFF/ON, output side — explains why the negative test above
found nothing: Tx is off by default, not because no mechanism exists).

Tested live: with the device's **Control Channel = CH12**, sent a plain Program Change on MIDI
channel 12 (program 0, then program 3) via a one-off `mido` script. **Confirmed: this switches the
Scene on the device** (user watched the screen, saw it change both times). Same pattern as Clip
(`Program Change = target - 1`).

**This is a per-device/per-project setting, not a fixed value** — Control Channel can be set to
anything CH1–16 on the system settings screen, so the app should not hardcode `12`. Wired into
`ControlPanelComponent` as a user-configurable Control Channel selector next to the Scene control.

## Tooling note

`python-rtmidi`'s Windows backend can hang in `port.close()` shortly after a sysex receive.
`mc707.safe_close()` runs closes in a timeout-bounded daemon thread and every script force-exits
via `os._exit()` at the end rather than relying on normal interpreter shutdown. If you hit a hang
again, check for orphaned `python.exe` processes holding the MIDI port before re-running.

## Track tone/patch selection — Bank Select + Program Change confirmed NOT working (2026-08-31)

Tested whether a track's active tone (patch) can be remotely selected the same way Scene and Clip
are — sent Bank Select (CC0 MSB / CC32 LSB) + Program Change on Track 1's channel (1), current
patch "5th DLA Pad":
- Program Change alone, values 0/10/20/40/60/80/100/120 — no change.
- Bank MSB 1/2/3/5/10/87 (LSB 0) with Program Change 0 — no change.

User watched the device screen throughout both sweeps. **Confirmed: standard Bank Select + Program
Change does not remotely change a track's tone.** This lines up with what web research on the
official update docs and community reports (Roland Clan forums) suggested: the MC-707's Bank/PC-via-MIDI
feature is for the device *transmitting* MSB/LSB/PGM per-clip to control *external* gear, not for
receiving a remote tone change to its own tracks. Scene and Clip both work via received Program
Change (confirmed earlier) — track tone selection does not follow the same pattern.

**Follow-up SysEx discovery attempt (same session, after creating a fresh project for a clean
baseline):** identified Track 1's actual Track-Tone address as `0x30200000` (distinct from
`0x30000000`, which is Clip 1's tone — confirmed by name match, "5th DLA Pad" then "INIT TONE"
after the fresh project). Read a 54-byte Tone Common block and compared byte-by-byte between two
different tones ("5th DLA Pad" vs "INIT TONE") to find bytes that actually *differ* between
different-sounding patches (a necessary property for any real type/category/preset field):

| Offset | "5th DLA Pad" | "INIT TONE" | Differs? |
|--------|---------------|-------------|----------|
| `+0x15` | `69` | `64` | yes (Level, already known) |
| `+0x1F` | `00` | `0C` | yes |
| `+0x21` | `01` | `00` | yes |
| `+0x26` | `03` | `04` | yes |
| `+0x27` | `0C` | `09` | yes |
| `+0x28` | `02` | `02` | **no** |
| `+0x29` | `02` | `02` | **no** |

Tried writing to the two most promising differing candidates:
- `+0x27` (write `0x00`): **rejected** — read back unchanged (`0x0C`/`0x09` depending on project).
- `+0x26` (write `0x02`): **rejected** — read back unchanged (`0x04`).

Both writes were silently ignored by the device (no error, just no effect on read-back — exactly
the failure mode the brief warns about: "never assume a write succeeded without reading back").
`+0x28`/`+0x29` were ruled out separately — a value sweep (0/1/3/5/8) produced no confirmed audible
change, consistent with them being identical across genuinely different tones above.

**Conclusion: remote patch/tone-bank selection does not appear achievable via simple SysEx byte
writes on this device.** The two candidate bytes that actually differ between different tones are
both write-protected — consistent with tone type/category being structural data that requires an
atomic bulk operation (like the on-device Sound Browser performs) rather than a single-byte DT1
write. Not pursued further without a different approach (e.g. reviewing `mcpoker`/`mctools_basic`
source for anything relevant, or a full preset-dump write via `.SVZ` data per JDTools — a much
bigger undertaking requiring the actual ROM preset data, which isn't available to us).

## M2 sub-problem A solved — tone-slot addressing formula (2026-08-31)

Read the actual source of `Locriana/mcpoker` and its successor `Locriana/mctools_basic` (both
public domain / MIT, per `docs/BRIEF.md` §3, §10). `mctools_basic/zen_core_tools.py`'s
`ZenCoreTools.get_base_address()` gives an 8-entry table of per-track base addresses plus a fixed
per-clip stride:

```python
mc_track_base = [0x30000000, 0x30220000, 0x30440000, 0x30660000,
                  0x31080000, 0x312A0000, 0x314C0000, 0x316E0000]
tone_alignment = 0x20000
# clip_idx 0-15:
base_address = mc_track_base[track_idx] + tone_alignment * clip_idx
# no clip_idx -> the track's own "track sound" slot (one past the last clip):
base_address = mc_track_base[track_idx] + tone_alignment * 16
```

**Bug found in the upstream formula and fixed:** `mc_track_base` is not a uniform arithmetic
sequence in plain hex (`0x30660000 -> 0x31080000` jumps by `0xA20000`, not the `0x220000` between
every other pair) because Roland SysEx addresses are 7-bit clean — every one of the 4 address
bytes must be `0x00-0x7F`. Adding `tone_alignment * n` with plain 32-bit hex arithmetic is only
correct while no byte crosses `0x7F`; past that point the real device address carries at `0x80`
(128), not `0x100` — i.e. the 4-byte address is base-128 digits, not base-256. mcpoker's own
Python code does the addition in plain hex and is silently wrong for the slots where this
matters. Implemented the correct base-128-carry addition in `tools/zencore_addressing.py`
(`add_7bit_carry()` + `get_slot_address(track_idx, clip_idx)`).

**Validated four ways, live:**
1. Formula reproduces the already-confirmed **Track 1 track-sound = `0x30200000`** (this session,
   earlier — see above) exactly.
2. Formula reproduces the already-confirmed **Track 4 track-sound = `0x31060000`** (`SynthChord01
   MC`, confirmed by on-device name match — see above) exactly. Plain hex addition gets this one
   wrong (`0x30860000`, an address containing an illegal `0x86` byte) — this is the case that
   caught the bug.
3. New prediction **Track 8 track-sound = `0x320E0000`** (needs the carry, never tested before):
   RQ1-read returned a real 16-byte structured reply (`20 20 ... 20`, i.e. a blank/space-padded
   name — a valid empty-slot name, not a timeout or garbage).
4. New prediction **Track 8 clip 10 = `0x32000000`** (also needs the carry): same result, a valid
   16-byte blank-name reply.
5. Attempting the *uncorrected* plain-hex address for the Track 8 case (`31 8E 00 00`) doesn't
   even reach the device — `mido` itself raises `ValueError: data byte must be in range 0..127`
   building the SysEx message, since `0x8E` has bit 7 set. Concrete proof the naive port is not
   just imprecise but outright broken for the affected slots.

**Affected tracks** (where `mc_track_base[track_idx]`'s byte2 is high enough that some clip_idx —
or the track-sound slot — overflows `0x7F` and needs the carry): **Track 4** (clip_idx ≥ 13, i.e.
clips 14-16 where "16" = track-sound) and **Track 8** (clip_idx ≥ 9, i.e. clips 10-16). All other
tracks stay under `0x7F` for every slot, but `tools/zencore_addressing.py` always applies the
carry-safe addition rather than relying on which tracks happen to be safe.

Within-tone field offsets (e.g. Level `+0x15`, Pan `+0x16`) don't need this correction — they're
added to a slot base address whose low byte is always `0x00`, and the largest known field offset
(`0x36`, the Tone Common block size) never approaches `0x7F`.

**Sub-problem A is solved.** `tools/zencore_addressing.py::get_slot_address(track_idx, clip_idx)`
gives the correct base address for any of the 8 tracks × (16 clips + 1 track-sound) = 136 tone
slots.

## M2 sub-problem B — within-tone field layout, first pass (2026-08-31)

Downloaded the official **JUPITER-Xm MIDI Implementation Ver.3.0+** PDF directly (`pdftotext`,
poppler's `pdftoppm` isn't installed here) rather than relying on the earlier-session summarized
read of it, since a summary isn't reliable enough to build hardware-write addresses from. Extracted
the exact byte tables (not paraphrased) for `[Tone Common]`, `[Tone Partial]` (offsets `0x00-0x4F`;
the rest of that block is out of scope this pass), `[Partial Filter Env]`, `[Partial Amp Env]`, and
`[Partial LFO]`'s first LFO (offsets `0x00-0x1D`; the block is oddly double-strided — `0x0200` not
the usual `0x0100` — almost certainly because it also holds a Mod LFO copy whose offset isn't
identified yet). Also pulled the `[ZCore]` section's authoritative block-offset table (relative
addresses for MFX, PMT, all 4 partials' Pitch/Filter/Amp envelopes, LFOs, EQs, and the Synth
Common/PMT/Partial blocks) — this resolved a discrepancy in `docs/BRIEF.md`'s summarized version:
**Partial LFO strides `0x0200` per partial, not `0x0100`** like every other per-partial block.

Wrote this all into **`data/zencore-map.json`** (the brief §6 deliverable), with an explicit
per-field `validation` tier (`audible-confirmed` / `round-trip-confirmed` / `structural` /
`unvalidated`) so app code (M3+) can tell a solid offset from a plausible-but-unverified one, plus
`meta.slot_addressing` (sub-problem A's table/formula) and `meta.block_relative_offsets` (the ZCore
map above) so the whole 136-slot × N-block address space is derivable from the file alone.

**This also retroactively explains two open mysteries from the earlier patch-selection SysEx
session** (see "Track tone/patch selection" above):
- `+0x26`/`+0x27` (the writes that were silently rejected) are actually the 3rd/4th nibble-bytes of
  **Portamento Time**, a 4-byte nibble-packed field starting at `+0x24`. The device most likely only
  accepts a DT1 write at a field's declared start address spanning its full byte width, not into the
  middle of a packed multi-byte value — the "silent no-op" failure mode matches this exactly.
  Confirms the earlier conclusion (don't remote-select tones this way) was right, but for a
  different, now-understood reason — it wasn't a structural/protected byte, just a malformed write.
- `+0x28`/`+0x29` (the value-sweep that showed no consistent audible effect) are **Bend Range
  Up/Down** — correctly explains why sweeping them produced nothing on a held static note.

**Live structural-pass validation** (`tools/validate_map.py`, read-only, RQ1 every declared field
and check it decodes in-range): ran against Track 1 Clip 1 for all 4 mapped blocks —
**97/97 fields passed** (`tone_common` 29/29, `tone_partial` 45/45, `partial_filter_env` 14/14,
`partial_amp_env` 9/9). Every nibble-packed multi-byte field decoded to a sane in-range value
(e.g. Tone Common's Portamento Time raw `00 00 04 09` → 73, well within 0-1023), confirming the
nibble4/nibble2 decode logic and the field-start-offset bookkeeping are both correct. The decoded
envelope shapes look real, not garbage: Filter Env `Level0..4` = `0, 1023, 1023, 1023, 0` (rises
then falls back to 0), Amp Env `Time1..4` = `0, 30, 400, 400` with `Level1..3` = `1023, 1023, 1023`
(fast attack, long decay/sustain/release) — plausible envelope data for whatever tone currently
occupies that slot, not noise.

**Tooling note:** the structural pass needs the MIDI output port, which the JUCE app holds
exclusively while running (Windows WinMM ports are single-client) — close the built app before
running `validate_map.py` or any of the other `tools/` scripts, same as the existing python.exe
port-lock gotcha above.

**Not done this pass (left for a follow-up M2 session):** the audible pass (write → listen →
round-trip → restore) for the priority subset — filter cutoff/resonance, envelope stages, LFO
rate/depth are all mapped and flagged `unvalidated`/priority-noted in the JSON, but not yet
confirmed by ear; Tone MFX and Tone PMT fields (declared address-only, explicitly deferred per
`docs/M2_PLAN.md`); the rest of `Tone Partial` past `0x4F`; Mod LFO's offset within the doubled
`partial_lfo` block; Partial EQ and Partial Pitch Env (headers found in the doc, not yet
transcribed); Synth Common/PMT/Partial and Reverb/Chorus (not started).
