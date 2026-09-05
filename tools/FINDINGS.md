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

**Revisited 2026-09-03, still not found.** Reading the Jupiter-X/Xm doc's full `3. Parameter Address
Map` for an unrelated investigation (factory ROM patches, see below) surfaced the same `00 05 00`/
`00 06 00` Master EQ/Comp offsets already tried in point 1 above — this wasn't new information, but
re-reading `mctools_basic`'s `sys_ex_comm.py` in full (not just `hack_tools.py`) this time turned up
something genuinely new: a **confirmed-working** reference address in its own demo code, Track Color
at `0x10000819` (+`0x200` per track) — re-verified live against this device before acting on it
(returned `12`, a plausible color index). That's real evidence `0x10000000`+ is genuine structured
settings memory, motivating a wider retry of the region point 2 above only scanned to `0x10004000`.

Ran `tools/scan_master_eq_wide.py` (100-byte chunks instead of 16, to cover more ground per round
trip) across `0x10000000`-`0x10077F38` (~490KB of address range). **Still zero candidates.** More
telling: only ~3.5KB of that ~490KB range actually replied with anything at all — real, structured
data (matching the Track Color find) is clustered in a narrow band right at the start, and everything
past roughly `0x10005000` was consistently empty/non-responsive, all the way out to where the scan
stopped. So the "settings" region isn't secretly bigger than originally scanned — the original
scan's boundary was already close to the real one. Master EQ isn't in it.

**Genuinely exhausted now**, not just parked on a hunch: both of the two independently-sourced,
concretely-motivated candidate regions (Jupiter-Xm's literal address, and `mctools_basic`'s own
settings region, now scanned 30x wider than before) have been tried and ruled out. What's left
untried, noted for whenever there's a new lead rather than more guessing: whether the device's native
`TOTAL EFFECTS EDIT` screen is reachable through a different SysEx command class entirely (not
RQ1/DT1) — would need MIDI-monitoring the device while opening that screen or changing a value on it,
capturing whatever the device itself sends, rather than more address-space scanning.

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

## Remote patch/tone selection — re-investigated, original negative finding upheld (2026-09-03)

User requested a per-track (×8) Category + Patch Name selector on the Control Panel, backed by
genuine remote patch loading against the device's full factory ROM set (a static data table, zero
device interaction to build the list — not a user-curated snapshot library). This reopened **"Track
tone/patch selection — Bank Select + Program Change confirmed NOT working" (2026-08-31, above)**.

**False start, corrected same day:** an initial retest (Bank Select MSB=87 LSB=92 + Program Change
4, then 5) appeared to load "A-005" and "A-006 Wub Bass" onto Track 1, visually matching the names
expected from the Jupiter-Xm MIDI Implementation doc's factory bank table (PR-A = MSB 87, LSB
92-93). This was written up here as a reversal of the 08-31 finding, gated by a "Rx Bank" device
setting. **That conclusion was wrong** — see below. Left as a lesson: a match against a predicted
name is not the same as a controlled test, and doesn't rule out an unrelated mechanism producing
the same-looking result by coincidence.

**Actual mechanism, confirmed live:** the MC-707's own official
[MIDI Implementation Chart](https://static.roland.com/assets/media/pdf/MC-707_MIDIImpleChart_eng03_W.pdf)
states plainly, in a footnote already fetched before the false start above but not read closely
enough at the time:

> \*2 Scene change (0-127) on control channel, Clip change (0-15) on track channel.

Program Change on a track's own channel selects that track's **clip slot (0-15)** — never the
track's own base tone. Confirmed decisively two ways:
1. **Sound Source** (`TONE TRACK SETTING-TRK:n` → GENERAL tab) is a per-track toggle between
   `TRACK` and `CLIP` — it controls whether the Tone Edit screen (and the audible sound) follows
   the track's own tone or whichever clip is currently selected. It was sitting on `CLIP` for all
   of today's testing, which is why Program Change appeared to change the "tone" — it was actually
   just switching clips, and the clip happened to already contain a tone matching the predicted
   name (this project's clips 4/5 apparently already had "A-005"/"A-006"-named content saved into
   them from earlier, unrelated work).
2. With Sound Source explicitly forced to `TRACK`, Track 1's own tone read **"SysCtrlDeepEcho2"**
   — unrelated to any bank/PC value tried — and stayed exactly that across every Bank Select + PC
   combination sent today, including MSB=87/LSB=92 with PC=3, 4, 5, and 100 (100 is outside the
   valid 0-15 clip range specifically to rule out clip-selection as an explanation — it had zero
   effect, as expected if Bank Select does nothing on this device). The track's own tone was never
   once touched.

**Conclusion: the 08-31 finding stands. Remote loading of a factory ROM tone onto a track's own
tone slot via Bank Select + Program Change is not achievable on the MC-707** — matches 08-31's own
web research (MC-707's Bank/PC-via-MIDI is for the device *transmitting* per-clip MSB/LSB/PGM to
control external gear, not receiving a remote track-tone change). The Jupiter-Xm doc's factory bank
table and "Rx Bank" parameter do not apply here; no such parameter exists on the MC-707 (confirmed
absent from both its official 121-page Reference Manual and a live, exhaustive photo walkthrough of
every row of `SYSTEM SETTING → MIDI`).

**A related, genuinely viable mechanism does exist, but doesn't meet the user's stated requirement.**
Program Change reliably selects a track's clip (0-15, confirmed), and each clip slot has its own
independently-addressable tone (already fully solved by this project's own M2 addressing work —
`get_slot_address(track_idx, clip_idx)` for `clip_idx` 0-15). So a "patch selector" could write any
of our own `data/zencore-map.json`-described tone data into a clip slot via SysEx, then switch to
it with Program Change. But this writes *our own* tone parameters, not a Roland factory ROM patch —
loading an actual factory patch this way would require first capturing every factory patch's raw
SysEx tone bytes (stepping through the Sound Browser once per patch, reading it back), which is
exactly the "zero device interaction to build the list" the user already ruled out when scoping
this feature. Parked; revisit only if the user wants to reconsider that constraint.

## Third-party reference: "B67 ZEN EditorLib" confirms addressing, reveals real transfer mechanism (2026-09-03)

User supplied the Quick Start guide and full manual for a commercial third-party MC-707/MC-101 ZEN-Core
editor/librarian ("B67 ZEN EditorLib", benis.it) for comparison. Two things stand out.

**Independent confirmation of this project's own addressing model, from a completely different codebase:**
the manual states MC-707 "is able to store and play up to a maximum of 17x8=136 ZEN-Core Tones: each of
the 8 tracks can store 16 ZEN-Core tones, one per clip, if Sound Source=Clip, [or] 1 ZEN-Core tone if
Sound source=TRACK" — exactly the 17-slots-per-track (16 clips + 1 track sound) × 8 tracks = 136 model
`tools/zencore_addressing.py`/`ZenCoreAddressing.{h,cpp}` already implements, arrived at completely
independently (by reading `mcpoker`/`mctools_basic` source, not this tool). Strong external validation.
Also independently confirms the **Sound Source=TRACK/CLIP** distinction discovered live earlier today,
including that it's a genuine, well-known, per-track setting (not something obscure to this session).

**Reveals how real tone loading actually works — and it isn't Bank Select.** The tool never sends Bank
Select or Program Change to load a tone. It maintains its own tone database and, per its own description,
"the dialogue between Librarian and Groovebox is in real time and it is possible to transfer each tone to
MC-101 and MC-707 in a fraction of a second" via a "Send Tone to MC-707" action — i.e. a **direct bulk
SysEx write of the tone's full parameter block into the selected Track/Clip's tone address**, the same
category of operation this project's own `ToneEditComponent` already does one field at a time. This
fully explains why Bank Select never did anything (confirms rather than contradicts the finding just
above) and, more importantly, means **remote tone loading onto a specific track/clip *is* achievable —
just via bulk SysEx write to the already-solved slot address, not via MIDI Bank Select/PC.**

**This does not, however, hand us Roland's actual factory ROM patch data.** The tool's bundled library
is the vendor's own "B67"-branded content (visible throughout the manual's screenshots: "B67 AnPad Big1",
"B67 JunoDetuned", etc.) plus optional purchased expansion packs (`B67_Collection_X`) and user-imported
`.svz` files (Zenology Pro / other ZEN-Core-compatible synths' own export format) — not a dump of the
MC-707's internal PR-A/B/C/D banks. So the "zero device interaction against the real factory ROM" goal
from the original feature request is still unresolved by this reference; what's now clearly unblocked
is the "write our own tone library into any track/clip slot" version parked in the section above.

**Missing-functionality scan against this project's current Tone Edit tab**, for future consideration
(not yet scoped or committed to, just captured while the manual was open):
- **Bulk tone read/write** ("Groovebox→Editor" / "Send Tone to Synth"): transfer an entire tone's fields
  in one operation, vs. this app's current per-field read/write. Directly reusable groundwork: iterate
  `ParameterMap`'s full field list for one address range instead of one field's descriptor.
- **Macro controls**: drive the same parameter (e.g. Cutoff) across a chosen combination of partials in
  parallel from one control, rather than editing each partial's copy individually.
- **Copy/paste**: partial→partial (whole partial, or just its AMP/Filter/Pitch envelope) and MFX
  effect→clipboard, persisting across tone loads.
- **PCM wave/sample browsing** (`PCM - Range - Common`): this app has no oscillator *sample* selection
  UI at all yet (only VA waveform/PW/PWM, per `synth_partial` — PCM partials, wave group/number, aren't
  mapped in `data/zencore-map.json` at all currently).
- **Keyboard/Velocity Range** per partial: key range, key fade, velocity range/fade, damp pedal fields —
  not currently in the map.
- **Tone/Category naming** (16-char name edit, Category dropdown): this app reads Tone Common's fields
  but the Tone Edit tab has no name/category editor.
- **A local tone library/file format** (`.syx` single-tone save/load, or a larger custom bank format):
  this app has no save/load of a captured tone to/from a local file at all yet — every session is
  live-device-only, nothing persists locally. Would need bulk-read support (above) as a prerequisite.
- **Project-level editing**: track EQ, Master Compressor/MFX/EQ — this is the same ground as the parked
  "Total Effects (MFX/COMP/EQ) — parked, unresolved" section above; the manual doesn't reveal the
  addresses (it's a black-box binary, not source), so it doesn't unblock that scan, just confirms the
  feature exists and is user-facing on real hardware.
Not prioritized or scoped — listed for whenever the user wants to pick the next piece of M3/M4 work.

## Tone Snapshot — bulk read/write, and the `synth_common` gap (2026-09-03)

Built the first item off the missing-functionality scan above: `src/model/ToneSnapshot.{h,cpp}`
reads/writes every field `data/zencore-map.json` currently declares in one operation (not one field
at a time), for any track or clip slot, plus save/load to a local JSON file. New `ToneEditComponent`
"Snapshot" section: Capture / Write To Selection / Save.../ Load..., and a Clip selector (Track Sound
or Clip 1-16) extending every existing per-field control to clip tones too, not just track sound.

**Write pacing bug, found and fixed:** the first working version fired all ~30 chunk writes in a
tight loop with no delay. A tone captured from Track 1 and written to Track 4 showed the correct
Name/Category but was completely silent — `tone_common` (sent first) landed, later chunks
(oscillator/envelope/partial data) didn't. Matches a documented MC-707 SysEx quirk (the B67 ZEN
EditorLib manual warns its own bulk transfers need pacing for "anomalies... in the SysEx
implementation of MC-101 and MC-707"). Fixed with `PacedSnapshotWriter`, a self-deleting
`juce::Timer`-driven helper sending one chunk per 40ms tick instead of a synchronous loop (see
`ToneSnapshot.cpp`). 40ms is a first, untuned guess kept in `kWriteIntervalMs` — revisit if a future
cross-track write is unreliable again.

**Second bug, same symptom, different cause: the real `synth_common` gap.** Even after pacing was
fixed, a self-write (Track 1 → Track 1) worked but a cross-write (Track 1 → Track 4) was still
silent — with every currently-mapped byte confirmed identical between source and destination (a
direct diff of two saved snapshot files, zero differences across all 32 chunks). Decisive test: the
device's own **native** Track Edit → SOUND → COPY/PASTE (no app involved) correctly transferred
sound Track 1 → Track 4, proving Track 4 itself was never the problem, and that the native mechanism
moves something our snapshot didn't. That pointed at `synth_common` (`meta.block_relative_offsets`,
relative offset `0x003C00`) — a block address that's been known since M2 but was never given any
`fields`, so it silently never appeared in the chunk plan at all. A self-write "works" trivially
(every unwritten byte is simply left as its own already-correct value); a cross-write leaves the
destination track's own stale `synth_common` sitting next to the newly-written oscillator/partial
data from a different tone, which is what actually broke playback.

Added `blocks.synth_common` with a first-guess 32-byte size — capturing it then failed with a
timeout-looking error. Direct RQ1 probes (`tools/mc707.py`, bypassing the app) at sizes
8/9/10/12/16/20/24/32 found the real cause: **the device itself caps the reply at exactly 9 bytes
for this address, for every requested size from 9 up** (8 gets 8, everything ≥9 gets exactly 9) —
not a timeout, a short reply our size-match check correctly rejected. Corrected the map to `size: 9`
(`validation: "structural"` — device-confirmed *extent*, not yet confirmed *meaning*; the 9 bytes are
declared as generic `Reserved 0`-`8` pending discovery). **After this fix, the cross-track write
produces correct sound**, confirmed live, matching the native copy/paste result exactly.

Takeaways for future block-boundary discovery: RQ1 requesting more than a block's real size doesn't
error or hang — the device just silently truncates the reply to what actually exists, which is
itself a legitimate, fast way to *find* an unknown block's true size (sweep sizes until the reply
stops growing), used here for the first time this session. Worth remembering next time a currently
`size: null` block (`tone_mfx`, `tone_pmt`, `synth_pmt`, `synth_partial`, `tone_partial`,
`partial_lfo`) needs its real extent nailed down instead of only its individually-named fields.

**Writing to a Clip slot — retested 2026-09-05, could not reproduce the original failure.** The
original report (Track 1 Track Sound → Track 1 Clip 2 not producing correct playback) was parked
undiagnosed. Retested methodically: (1) read-only check on Clip 1 — real data, no bug; (2) self-write
Track 1 / Clip 1 → itself — worked; (3) the actual failing scenario, Track 1 Track Sound → Track 1
Clip 1 — captured both sides to files and diffed byte-for-byte (all 33 chunks, name, and category):
**zero differences**, and the user confirmed Clip 1 sounds correct afterward. Clip writing is not
broken. The original failure was very likely a transient connectivity hiccup, not a code bug — this
exact session hit several unrelated timeout-then-clean-retry cases today (the Name field write test,
a plain RQ1 read that failed twice then succeeded identically on the third try) with no code changes
in between, so a one-off failure during the original clip test isn't surprising in hindsight. No
code or map change was needed here; this note replaces the earlier "parked, undiagnosed" one.

## Factory ROM patch browsing — no direct-read address found, likely architecturally closed (2026-09-03)

Revisited the parked "full ROM patch browser" goal with a concrete proposal: get the official factory
patch list (name + category, zero device interaction) and look for a systematic relationship to some
SysEx address or numbering scheme, so only a handful of spot-checks would be needed to validate it
rather than manually capturing all ~1000+ patches. Two inputs: the official
[MC-707 Sound List](https://static.roland.com/assets/media/pdf/MC-707_SoundList_multi01_W.pdf) (Bank
A #1 is literally "SysCtrlDeepEcho2" — the exact name captured from Track 1's track-sound slot during
today's Tone Snapshot debugging, confirming our existing RQ1 read mechanism *does* return real,
byte-accurate factory ROM data whenever a ROM patch happens to already be sitting in a readable slot);
and the Jupiter-X/Xm MIDI Implementation doc's full "3. Parameter Address Map" (§3, not just the Bank
Select subsection used earlier), read in full for the first time this session.

**No separate, directly-readable address block for factory/preset tones exists in that map.** It
declares exactly two tone-shaped regions: `Temporary Tone` (`02 00 00 00`+, the live edit buffer —
this is the ZCore address family our own addressing already lives in) and `User Tone` (`50 00 00 00`+,
the writable/savable 512-slot user bank, one tone per `0x20000` — the same stride our own
`tone_alignment` already uses, interesting but a different, writable region). PR-A/B/C/D and the other
factory banks appear *only* as Bank Select CC values, never as their own address range — meaning even
on Jupiter-X/Xm, a factory patch is only reachable by loading it into Temporary Tone via Bank
Select+Program Change, exactly the mechanism already proven today to not reach MC-707's track tone
(Program Change there selects a clip instead — see "Remote patch/tone selection" above).

**Cross-checked against `Locriana/mctools_basic`'s own source** (`zen_core_tools.py`, `sys_ex_comm.py`,
`midi_if.py`, `hack_tools.py` — downloaded and grepped for `preset`/`rom`/`factory`/`bank`): nothing
beyond the standard Bank Select CC constants already known, and no distinct ROM/preset address logic
anywhere. This is a real, hands-on hacking effort on this exact device (the same source that gave us
the track-base table and the Total Effects candidate region) — if an undocumented direct-read path
existed and were easy to find, this is the kind of project that plausibly would have already found it.

**Conclusion: not just impractical, likely architecturally closed** — there doesn't appear to be a
number scheme to spot-check, because there's no evidence a fixed address for factory ROM content
exists at all on this product family; it seems to only ever get reached by a load-into-edit-buffer
operation, and MC-707 doesn't expose that operation for its own track tone. Still open: whether the
native `TRK EDIT → SOUND → COPY/PASTE` mechanism (confirmed working today, native copy Track 1 → Track
4) is triggered by some *other* SysEx command class (not RQ1/DT1) that could be scripted — not
investigated, no evidence either way, would need a fresh capture (e.g. MIDI-monitor the device while
performing a native copy) rather than more address-map reading.

**Real, unrelated side-discovery worth pursuing separately:** the same address map read for this
investigation shows a `System` block at `00 00 00 00` (Jupiter-X/Xm numbering) with `Master EQ` at
offset `00 05 00` and `Master Comp` at `00 06 00` — a completely different region than the
`0x10000000` area scanned and abandoned in the parked "Total Effects" investigation above (that
scan's candidate region came from `hack_tools.py`'s own `dump_settings()`, unrelated to this map).
Not yet translated to an MC-707 address or tested live — MC-707's own top-level scheme is already
known to differ from Jupiter-X/Xm's (same lesson as the ZCore tone addresses) — but this is a
genuine, sourced, unexplored lead, not a blind guess, and a much better starting point than the
region already ruled out.

## MFX Chorus Send Level — second confirmation attempt, still inconclusive (2026-09-03)

Retried closed-loop confirmation of `tone_mfx`'s Chorus Send Level (`+0x02`) on Track 1's track
sound, previously only `structural` ("didn't locate a dedicated Chorus Send screen for MFX"). Read
127 (max, matching the earlier note exactly — reproducible). Wrote 64 via SysEx, round-trip
confirmed at the byte level; user found no on-device display anywhere that changed. Restored to 127
and tried an audible A/B instead — also no perceptible difference. Checked whether MFX being off
could explain a null result on both counts: it isn't — MFX Switch read `1` (ON), Type read `32`
(one of the 91 effect types, not looked up). So this is a genuine dead end by two independent
methods, not a missed screen or an oversight — something about how this field actually behaves
(maybe Type-dependent whether Chorus Send matters at all, maybe MC-707's UI just doesn't expose it,
matching the pattern already seen elsewhere in this map) isn't understood yet. Stays `structural`.
Reverb Send Level (`+0x03`) wasn't separately retested — same open question likely applies. Next
attempt, if any: try a different MFX Type known to route through Chorus/Reverb sends audibly, rather
than retesting the same tone/Type combination a third time.

## Tone Name / Category editing — fully confirmed working (2026-09-03, closed 2026-09-05)

Built off the missing-functionality scan's top pick: `ToneEditComponent` gained a header-row
`nameEditor` (`juce::TextEditor`, commits on Enter/focus-lost, not the sliders' 50Hz poll-and-send —
a text field has no natural "diff against last value") and `categorySlider`, reading/writing
`tone_common`'s Name (ascii, `+0x00`) and Category (uint7, `+0x10`) via the same
`computeAddress`/`sendSysExRead`/`sendSysExWrite` machinery every other field already uses. New
`SysExCodec::encodeAscii`/`decodeAscii` (added earlier today during Tone Snapshot, unused until now)
handle the ascii encoding.

**Reading is fully confirmed working.** First test showed blank Name / Category 0 — but that was a
one-off transient connectivity issue, not a code bug: independently reading the exact same address
via `write_field.py` moments later returned real data ("HiPitch Buzz Ld" / Category 34), and after
adding proper error-status surfacing to the new read callbacks (they'd been silently swallowing
failures, unlike every other control's read path — a real gap, now fixed) and rebuilding, a plain
Refresh click correctly showed "HiPitch Buzz Ld" / `34`, matching the independent check exactly.

**Writing is only half-verified, and parked before finishing.** A direct Python write to the same
address (`write_field.py --write`, bypassing the app) round-tripped perfectly at the byte level
(wrote "TESTNAME1234567", read back matched exactly) — so the underlying write mechanism is sound,
not a rejected/protected field. But the on-device screen never showed the new name, even after the
user was asked to back out and re-enter the screen (the already-known "device doesn't live-redraw"
quirk didn't explain it here). The user's last message before parking — "Clip is not renaming" —
suggests the on-app test was actually happening with the **Clip selector set to a specific clip**,
not Track Sound. That most likely means this was hitting the already-parked **"Writing to a Clip
slot does not work yet"** issue from the Tone Snapshot section above, not a new Name-specific bug —
but this wasn't confirmed before stopping. Test value was restored to the original "HiPitch Buzz Ld"
via direct Python write (round-trip confirmed) before parking, so the device is clean.

**Closed 2026-09-05.** After the Clip-slot write bug above was retested and couldn't be reproduced,
Name-field editing was retested the same way (Clip 1 selected, direct edit + Enter) and now updates
the on-device name correctly. Confirms the suspicion: both "not working" reports were the same
transient connectivity issue, not a Name-specific bug or a real Clip-write bug. No code changes were
needed for either — Tone Name/Category editing is fully working, reading and writing, for both
Track Sound and Clip slots.
