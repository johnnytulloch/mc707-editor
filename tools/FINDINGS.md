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

## Total Effects (MFX/COMP/EQ) — parked, unresolved (2026-09-02)

Found a second "TOTAL EFFECTS EDIT" screen (tabs: MFX / COMP / EQ), separate from both the per-tone
MFX already mapped (`tone_mfx`) and the MIX screen mystery above — this is a **global** effects
chain, not tied to any track/tone. Read the doc structure for all three tabs:

- **EQ = "Master EQ"** (doc section `[Master EQ]`, 15 bytes): Input/Low/Mid1/Mid2/Mid3/High Gain
  (each 1 byte, range 40-88, `display = raw-64` dB), Low/Mid1/Mid2/Mid3/High Frequency (each 1 byte,
  enum index 0-29 into a fixed table: 20,25,31,40,50,63,80,100,125,160,200,250,315,400,500,630,800,
  1000,1250,1600,2000,2500,3150,4000,5000,6300,8000,10000,12600,16000 Hz), Mid1/Mid2/Mid3 Q (each
  1 byte, enum 0-5: 0.5,1.0,2.0,4.0,8.0,16.0), Switch (1 byte, OFF/ON).
- **COMP = "Master Comp"** (doc section `[Master Comp]`): a 3-band (Low/Mid/High) compressor, each
  band with Attack Time, Release Time, Threshold, Ratio, Knee, Output Gain, plus an overall Switch.
- **MFX** reuses the exact same `[MFX]` structure already mapped for tone-level MFX (Type/Switch/
  Chorus+Reverb Send/32 type-dependent Parameters) — confirmed by cross-referencing the doc's own
  top-level map, which references `[MFX]` for "Scene MFX (01-05)" too, the same generic block used
  in multiple contexts. "Multi Mode Filter" (Filter Type/Tone/Color/Slope, seen live on-device) is
  just one of the ~91 possible types, same pattern as tone MFX's "Low Boost".

**The catch: these are global "System"-area settings, not ZCore tone data** — completely outside
the already-solved `0x30xxxxxx` tone-slot address space. Tried to locate the real MC-707 address:

1. Jupiter-Xm's own documented `System` addresses (`00 05 00 00` for Master EQ, `00 06 00 00` for
   Master Comp) — **no reply at all**, as expected (MC-707's own top-level scheme already proven
   different from Jupiter-Xm's for ZCore).
2. `Locriana/mctools_basic`'s own `hack_tools.py::dump_settings()` uses `0x10000000`-`0x10004000`
   as its "settings" scan region (empirically sourced from the same toolkit that gave us the correct
   ZCore track-base table) — a strong, non-arbitrary lead. Live-read exact current values off the
   device's EQ tab (Input Gain +15dB, Low Gain 0dB, Mid1 Gain 0dB, Mid3 Gain +6dB, High Gain +3dB,
   Low Freq 200Hz, Mid3 Freq 1000Hz, High Freq 5000Hz, Mid3 Q 1.0) and built a 9-byte exact-value
   signature (`{+0x00:79, +0x01:64, +0x02:64, +0x04:70, +0x05:67, +0x06:10, +0x09:17, +0x0A:24,
   +0x0D:1}`) via `tools/scan_master_eq.py` (uses `add_7bit_carry` throughout, like all ZCore
   scanning must — caught the same naive-`+=`-overflow bug the original ZCore scan hit, fixed the
   same way). Two full scans of the region (a loose range check, then the precise 9-byte signature)
   both found **zero candidates** across ~4100-11000 collected bytes (the region only responds for
   roughly its first quarter to third before timing out for the rest, suggesting the "settings"
   region is smaller than the full `0x4000` `hack_tools.py` scans, or non-contiguous).
3. Quick probes at a few adjacent/nearby bases (`0x11000000`, `0x12000000`, just past the scanned
   range) — no reply at any of them.

**Conclusion: not found.** Every motivated lead is exhausted; further search would be unstructured
guessing across a large address space with no signal to follow. Parked by explicit user choice.
`tools/scan_master_eq.py` (kept in the repo) is reusable for testing a different candidate region if
a better lead turns up later — swap `start`/`SIGNATURE` for a new hypothesis.

## MIX screen — parked, unresolved (2026-09-02)

The device's Tone Edit screen has a "MIX" tab (LEVEL/PAN/DELAY/REVERB) distinct from AMP/FILTER/
EQ/MFX, tested on Track 8 Partial 1 ("Ax Fantasia"). LEVEL (127) happened to match `tone_partial`'s
already-mapped Level field — likely coincidental (both at a common max/default value), not a real
confirmation, given what follows. PAN, DELAY, and REVERB could not be located:

- DELAY and REVERB both show a **`>` prefix** on-screen (`>DELAY 56`, `>REVERB 87`), unlike LEVEL/PAN
  — on Roland gear this typically marks a value currently being modulated, showing a live/computed
  number rather than a stored parameter. Both read back `0x00` from `tone_partial`'s Chorus Send
  Level (`+0x14`) / Reverb Send Level (`+0x15`), consistent with a modulated live value that a
  static RQ1 read can't capture (base 0, displayed value reflects active modulation on top).
- PAN was live-diffed properly (not just formula-guessed): dumped the full `tone_partial` block
  (`0x00-0x3C`) and `synth_partial` block (`0x00-0x1C`) for Track 8 Partial 1 while PAN read `05R`
  on-device, then again after the user set PAN to hard right (`63R`) on that same MIX screen and
  confirmed the on-device display changed — **not a single byte differed** in either dump between
  the two states. So this PAN is not stored in either block at all. Also ruled out: Tone Common's
  own Pan (`+0x16`, stayed at `0x40`/center throughout) and the Control Panel's CC10 Pan knob
  (moving it live did not change the MIX screen's PAN display) — so it isn't the already-known
  track-level Mixer Pan either (see Tone Partial's Pan field note above for that earlier, separate
  finding).

**Conclusion: this MIX screen's PAN (and likely DELAY/REVERB) live somewhere not yet located,**
possibly a genuinely different structure from anything mapped so far, or LEVEL is the only field on
this screen that's a plain ZCore parameter. Parked by explicit user choice rather than continuing an
open-ended address hunt. Whoever picks this up next: the diff methodology used here (dump a block,
change one value via a known on-device edit, dump again, look for the changed byte) is more reliable
than guessing a display formula and is worth repeating over a wider address range or a different
block entirely (Tone PMT's un-mapped bytes, or an address region not yet explored at all).

**Not done this pass (left for a follow-up M2 session):** the audible pass (write → listen →
round-trip → restore) for the priority subset — filter cutoff/resonance, envelope stages, LFO
rate/depth are all mapped and flagged `unvalidated`/priority-noted in the JSON, but not yet
confirmed by ear; Tone MFX and Tone PMT fields (declared address-only, explicitly deferred per
`docs/M2_PLAN.md`); the rest of `Tone Partial` past `0x4F`; Mod LFO's offset within the doubled
`partial_lfo` block; Partial EQ and Partial Pitch Env (headers found in the doc, not yet
transcribed); Synth Common/PMT/Partial and Reverb/Chorus (not started).

## M2 audible pass, round 1 — closed-loop device confirmation (2026-08-31, later same day)

Started with the classic write→listen→restore method (`tools/write_field.py`, a non-interactive
sibling of `dt1_write_test.py` for scripted use): confirmed **TVF Cutoff Frequency** (`+0x32`) and
**TVF Resonance** (`+0x3D`) audibly on Track 1's track-sound slot (`0x30200000`), each via a raw
MIDI Note On (channel 1, note 60) sent directly by script — matching the exact method that worked
for the original M1 Level test. Note: a read taken shortly after Note Off showed a drifted value
(510) instead of the written 100 for Cutoff — **reads should be taken with no note active/settled,
not immediately after Note Off**, since the raw address may reflect live/modulated state while a
note is sounding or just released. Also caught and fixed a real mistake here: a restore write typo
(`00 00 03 0F` instead of the actual original `00 03 0F 0F`) briefly left Cutoff at the wrong
value (63 instead of 1023) before being corrected on the next write.

**The user then proposed a much stronger protocol: closed-loop confirmation against the device's
own front-panel display**, not just "does the sound change" — (1) set a control on-device to a
known value, read the address back and check for an exact numeric match, then (2) write a new
value via SysEx and confirm the on-device display updates to match. This is strictly better
evidence than audible-only confirmation (removes all subjectivity, ties the address directly to
the device's own named parameter) and became the method for the rest of this pass. Added a new
`device-confirmed` validation tier in `data/zencore-map.json` for it — the strongest of the tiers.

**First finding from this method was a false alarm, usefully explained:** user set the device's
**Mixer screen** Pan to hard left; Tone Common's `+0x16` Pan still read center (0x40), unchanged.
This is *not* a bug — Track 1 already has a CC-based Pan control in the app's Phase 1 Control
Panel (almost certainly standard MIDI CC10, per `docs/BRIEF.md`'s own CC table), which is what the
Mixer screen edits. Tone Common's Pan (`+0x16`) is a separate, deeper patch-internal parameter not
exposed on the Mixer screen at all — both findings are correct, they're just different parameters.

**Confirmed via the closed-loop method (all bidirectional: on-device set → read matches; SysEx
write → on-device display updates; then restored to the original value):**
- **Tone Partial Pan** (`tone_partial +0x07`, per-partial, not Tone Common's `+0x16`): device's
  SOUND EDIT → AMP → PAN PLT1-4 screen. Set to L64, read back `0x00` exactly; wrote `0x40` (center)
  via SysEx, on-device PLT1 display updated to center.
- **TVF Cutoff Frequency** (`+0x32`) and **TVF Resonance** (`+0x3D`): device's Easy Mode FILTER
  screen (numeric 0-1023 display, distinct from the per-partial PLT1-4 AMP screen — Easy Mode
  appears to write the same Cutoff/Resonance value to all 4 partials at once, unlike Pan which was
  tested per-partial). Set to 492/27, read back matched exactly; wrote 800/500 via SysEx, on-device
  display updated to match; restored.
- **Envelope ADSR mapping, all three envelopes** (device's Easy Mode FENV/PENV/AMP screens show a
  simplified 4-knob Attack/Decay/Sustain/Release, not the full multi-stage raw data) — same pattern
  confirmed exactly on Filter Env, Amp Env, and (after fetching its byte table fresh from the doc,
  not previously mapped) Pitch Env:
  | Easy Mode | Raw field |
  |---|---|
  | Attack | Time1 |
  | Decay | **Time3** (not Time2 — Time2/Level0/1/2 exist but aren't exposed in Easy Mode; likely a hidden secondary stage) |
  | Sustain | **Level3** (the last level before the release stage) |
  | Release | Time4 |

  Filter Env: Attack=0→Time1=0, Decay=850→Time3=850 (round-trip write to 200 confirmed on-device),
  Release=564→Time4=564, Sustain=0→Level3=0. Amp Env: Attack=32→Time1=32, Decay=897→Time3=897,
  Sustain=36→Level3=36, Release=195→Time4=195. Pitch Env (also newly mapped into
  `data/zencore-map.json` this pass, full 45-byte/`0x2D` block from the official doc — Depth,
  Velocity Sens ×3, Time1-4, Level0-4 signed `-511..+511`, Velocity Curve): Depth=+6→raw 134,
  Attack=20→Time1=20, Decay=77→Time3=77, Sustain=+511 initially matched **both** Level0 and Level3
  (they happened to be numerically equal in this tone) — disambiguated by writing Level3 alone to a
  different value (raw 1024) and confirming only the Sustain display changed; Level3 confirmed,
  Level0 left `unvalidated`. Restored afterward.
- **Partial LFO, 6 fields** (`partial_lfo +0x00/+0x04/+0x17/+0x19/+0x1B/+0x1D` — Waveform, Rate,
  Pitch/TVF/TVA/PAN Depth): all matched the on-device LFO display exactly (Waveform=1/TRI, Rate=691,
  Pitch Depth=+20, TVF Depth=+19, TVA Depth=+8, PAN Depth=+6) — 6 independent numeric matches, read
  only (no write test this round).

**Important open finding, not resolved this pass:** the device's Easy Mode tone-edit screen shows
**two independent LFO tabs** ("LFO 1", waveform SIN, and "LFO 2", waveform TRI) with visibly
different Rate/Depth values on each. The block mapped above (relative offset `0x3000`, which the
official doc's own ZCore address-map table calls "Partial LFO **1**") is actually the data behind
the on-device **"LFO 2"** tab — confirmed by all 6 matches above. Reading the next partial-indexed
slot (`0x3200`, "Partial LFO 2" per the doc) to look for the UI's "LFO 1" instead returned data
**identical** to `0x3000`, not a second independent LFO. Two live hypotheses, neither confirmed:
(a) Easy Mode broadcasts LFO settings to all 4 partials identically (matches the same broadcast
behavior already seen for Cutoff Frequency), so `0x3200` really is partial 2's LFO and just
happens to mirror partial 1's; or (b) the UI's "LFO 1" lives in a completely different,
unexplored block — the doc's separate Tone Synth Common/PMT/Partial hierarchy (relative offsets
`0x3C00`+) is an untested candidate, especially since the tone under test this whole session,
"Ax Piano", is very likely one of Jupiter-X's analog Model Expansion tone types (a second,
parallel synth-modeling layer, distinct from the standard PCM partial engine) rather than a plain
ZEN-Core patch. Left as an open item per user's explicit choice to stop here and write up rather
than keep hunting.

All test writes this pass were restored to their original values and round-trip verified before
moving to the next field. `tools/write_field.py` (new) is the non-interactive read/write/round-trip
helper used throughout — same safety posture as `dt1_write_test.py` but scriptable without a
blocking stdin prompt, since audible/visual confirmation now happens via the user's own reports
rather than the script's own `input()`.

## M3 pilot — Tone Edit tab, first real SysEx UI in the app (2026-09-01)

Built the app's first real SysEx read/write panel (`src/ui/ToneEditComponent`, new "Tone Edit" tab):
5 controls (Attack/Decay/Sustain/Release/Pan, the AMP section confirmed above) for a selectable
Track (1-8) / Partial (1-4), addresses/offsets/ranges looked up from `data/zencore-map.json` at
runtime (embedded via JUCE `BinaryData`, see `CMakeLists.txt`) rather than hardcoded — see
`docs/`/`CLAUDE.md`'s "parameter map is data" rule. New supporting pieces: `SysExCodec.h` (RQ1/DT1
build + checksum + uint7/nibble2/nibble4 encode/decode, ported from `tools/mc707.py`),
`ZenCoreAddressing.{h,cpp}` (7-bit-carry slot addressing, ported from `tools/zencore_addressing.py`),
`ParameterMap.{h,cpp}` (JSON field lookup). `CCTransmitter` was extended (not replaced) to also open
a matching `MidiInput` and handle sequential RQ1/DT1 request-reply (queued one-at-a-time, per the
documented reply-address bug making concurrent requests undisambiguatable) — hoisted out of
`ControlPanelComponent` into `MainComponent` so every tab shares the one connection, since MC-707's
MIDI output is confirmed exclusive on Windows (a second independent connection attempt fails
outright).

**Tested live, all 5 controls confirmed working end-to-end** (Track 1, Partial 1): Refresh correctly
reads and populates real current values from the device; moving each slider produces an audible
change on Attack/Decay/Sustain/Release, and a confirmed pan shift on Pan.

**New device quirk found during this test:** the on-device Sound Edit screen does **not** live-update
its own displayed value while a SysEx write lands on the parameter it's currently showing — the
sound changes immediately (confirmed audible), but the on-screen number stays stale until you back
out of the edit screen and re-enter it, at which point it redraws with the correct, current value.
Not a bug in the app — a display-refresh characteristic of the device itself. Worth remembering for
any future closed-loop UI test: don't conclude a write failed just because the number on-screen
didn't move; exit/re-enter (or listen) to check.

Added a FILTER section the same way right after (Cutoff, Resonance, Filter Envelope ADSR) — all
already device-confirmed, no new discovery needed. Confirmed working live. Also hit and fixed a
real bug during this: `EditControl` holds a `juce::Slider`/`Label`, which are non-copyable *and*
non-movable, so `std::vector<EditControl>` can't work at all (not even `reserve()` + `emplace_back()`
— the container's move-construction requirement is checked at compile time regardless of whether a
reallocation ever happens at runtime) — switched to `std::vector<std::unique_ptr<EditControl>>`.

## LFO 1 / MDLSYN0 — resolved as out of scope (2026-09-01)

Chased the "LFO 1" mystery (see "M2 audible pass, round 1" above) one step further before parking
it. The official doc has a **completely separate top-level structure, `[MDLSYN0]` ("Analog Synth
Model Parameter")**, referenced from the Jupiter-Xm's own top-level address map (`3.Parameter
Address Map`) at `02 1T 00 00` ("Temporary Tone Analog Synth Model (01-04)") — entirely outside the
ZCore tone space (`02 0T 00 00` on Jupiter-Xm, `0x30xxxxxx` on the MC-707) we've been working in all
session. `MDLSYN0` contains a `Model` field (`---, JP8, JX8P, JUNO106, SH101` — Roland's "Model
Expansion" analog-modeled synths) immediately followed by `LFO WAVE FORM` / `LFO RATE` / `LFO DELAY
TIME` — almost certainly "LFO 1". The tone used for testing all session, "Ax Piano", is very likely
one of these Model Expansion tones (an entirely separate synth-modeling layer, layered alongside
the regular ZEN-Core/ZCore PCM engine whose parameters we've been mapping).

**Decision: not pursued.** Locating `MDLSYN0`'s real address on the MC-707 would require a fresh,
open-ended scan of a region we haven't touched at all (Jupiter-Xm's own top-level scheme doesn't
transfer directly — the MC-707 already proved to use its own distinct top-level addressing for
ZCore). User explicitly scoped the project to **ZCore tones only** going forward. The `partial_lfo`
block already mapped and device-confirmed (6 fields: Waveform, Rate, Pitch/TVF/TVA/PAN Depth) is a
genuine ZCore/ZEN-Core parameter (not MDLSYN0) and stays in scope — it's simply not literally what
the on-device "LFO 1" tab edits for a Model Expansion tone. `data/zencore-map.json`'s existing note
on `partial_lfo` already documents this distinction accurately; no changes needed there.
