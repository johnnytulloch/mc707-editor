# MC-707 Editor — Project Context for Claude Code

Read `PLAN.md` (phased implementation plan + evaluation of the brief) and `docs/BRIEF.md` (original
project brief) in full before doing anything else. This file is a handoff note on top of those —
it explains what's been tried, what's blocked, and why.

## Current status

**Phase 1 (CC Quick-Edit) is built and verified working against the real MC-707**, on Windows (see
"Why we're on Windows" below). Device appears in the MIDI Output dropdown, sliders audibly change
the sound.

**Phase 2/M1 (SysEx viability gate) has also passed**, live against the same device — see
`tools/FINDINGS.md` for full details (raw Identity Reply bytes, tone address layout discoveries,
the confirmed Tone Level byte offset, and a tooling gotcha around closing MIDI ports on Windows).
Summary: Identity Request replies (first independent capture), RQ1 reads reproduce the documented
reply-address bug exactly, and a DT1 write to a tone's Level byte produced a confirmed audible
silence/restore round trip. Per `PLAN.md`, M1 passing unblocks M2 (parameter map), scoped but not
yet started — see `docs/M2_PLAN.md`.

**The main UI is a 4-tab app**: Control Panel (8-channel mixer-style control surface, matching a
supplied mockup, CC/Program-Change only), File Transfer (SD-card SMF copy helper), Clip Matrix
(8×16 grid for bulk-assigning MIDI files to track/clip slots before copying), and **Tone Edit**
(new — the app's first real SysEx read/write UI, see below). All four confirmed working against
real hardware.

**M2 (parameter map) is substantially underway, not "not yet started."** `data/zencore-map.json`
now maps ~170 fields across 8 ZCore blocks (Tone Common, Tone Partial — including its Vibrato Sens
and 4-slot Matrix Control fields, Partial Pitch/Filter/Amp Envelopes, Partial LFO, Partial EQ, Tone
MFX, Synth PMT), most at the `device-confirmed` tier (closed-loop verified against the on-device
display, not just audible testing) — see `tools/FINDINGS.md`'s "M2" sections for the full discovery
log and methodology. Sub-problem A (tone-slot addressing, all 136 track/clip slots) is fully solved
— see `tools/zencore_addressing.py` and its C++ port. Deliberately out of scope / not pursued: MFX's
32 type-dependent parameter slots (each of ~91 effect types reinterprets them differently — no
per-type name table built), EQ Frequency/Q (raw-to-Hz/Q encoding not yet determined), Matrix
Destination2-4/Sens2-4 per slot (only each slot's first pair is mapped), and the "LFO 1" / `MDLSYN0`
Model Expansion structure (lives in a completely separate, unexplored address region — project is
scoped to ZCore tones only per explicit decision).

**M3 has working coverage of every tab on the device's own tone-edit screen.**
`src/ui/ToneEditComponent` is a real SysEx-editing panel — Track (1-8) / Partial (1-4) selectors,
and twelve labelled sections (OSC, STRUCT, MTX1-4, AMP, FILTER, LFO, PENV, EQ, MFX — 49 controls)
that read current values on Refresh and write on change, addresses/offsets/ranges all looked up from
`data/zencore-map.json` at runtime (embedded via JUCE `BinaryData`) — never hardcoded, per the
convention below. Confirmed working live for every section, on real hardware. One real bug was
caught and fixed along the way: a block-name mismatch between `data/zencore-map.json`'s `blocks`
and `meta.block_relative_offsets` tables silently resolved to a wrong-but-plausible address instead
of failing — `ParameterMap::blockRelativeOffset()` now `jassert`s on that class of mismatch.
Explicitly out of scope for this pilot: per-write read-back verification (initial Refresh read
covers the safety principle, not every single write), and a nicer display mapping for enum/dB/signed
fields (sliders currently show raw byte values, not the on-device unit).

**Tone Snapshot (bulk read/write) is built and confirmed working, including cross-track transfer.**
`src/model/ToneSnapshot.{h,cpp}` captures every field `data/zencore-map.json` currently declares for
a whole tone in one operation (not one field at a time), writes a captured snapshot to any other
track/clip slot, and saves/loads it to a local JSON file. `ToneEditComponent` gained a Clip selector
(Track Sound or Clip 1-16, addressing every existing per-field control as well as Snapshot) and a
"Snapshot" section (Capture / Write To Selection / Save.../ Load...). Two real bugs found and fixed
via live cross-track testing (Track 1 → Track 4, both Track Sound): unpaced writes silently dropping
most of a ~30-chunk burst (fixed with a timer-paced writer, see `PacedSnapshotWriter`), and a
genuinely missing block (`synth_common`, address known since M2 but never given fields, so never
appeared in any read/write) whose real 9-byte size was found by sweeping RQ1 read sizes rather than
guessed — see `tools/FINDINGS.md`'s "Tone Snapshot" section for the full trail. **Confirmed working
for both Track Sound → Track Sound and Track Sound → Clip transfers** (the latter retested
2026-09-05 after an initial failure didn't reproduce — see `tools/FINDINGS.md`; byte-for-byte diff
clean across all 33 chunks, confirmed audibly correct). Also out of scope so far:
a snapshot only contains what's already mapped (PCM wave/sample, keyboard/velocity range, and full
MFX type parameters aren't captured), and there's no library/browser UI yet, just capture/write/
save/load primitives.

**Tone Name / Category editing is built and fully confirmed working**, for both Track Sound and
Clip slots. `ToneEditComponent` gained a header-row `nameEditor` (`juce::TextEditor`, commits on
Enter/focus-lost rather than the sliders' 50Hz poll-and-send) and `categorySlider`, reading/writing
`tone_common`'s Name (ascii) and Category (uint7) fields — see `tools/FINDINGS.md`'s "Tone Name /
Category editing" section. Category shows the raw 0-49 number; no name lookup table exists (the
official Sound List gives per-tone category strings, not a number-indexed legend).

- `CMakeLists.txt` — JUCE via CMake FetchContent (JUCE tag 8.0.6), builds a standalone GUI app
  target `MC707Editor`; also a `juce_add_binary_data` target embedding `data/zencore-map.json`.
- `src/Main.cpp` — JUCE application/window boilerplate.
- `src/ui/MainComponent.{h,cpp}` — app shell hosting the 4 tabs. Owns the one shared
  `CCTransmitter` (MIDI connection) and passes it by reference into every tab that needs it — the
  MC-707's MIDI output is exclusive on Windows (confirmed empirically), so only one connection can
  exist at a time; tabs share it rather than each opening their own.
- `src/ui/ControlPanelComponent.{h,cpp}` + `src/ui/TrackStripComponent.{h,cpp}` — the control
  surface (8 `TrackStripComponent`s, LCD readout, Scene selector with user-selectable Control
  Channel). Full CC mapping table is in `PLAN.md`.
- `src/ui/ToneEditComponent.{h,cpp}` — the SysEx tone editor pilot described above.
- `src/midi/CCTransmitter.{h,cpp}` — wraps a `juce::MidiOutput`/`juce::MidiInput` pair: CC/Program
  Change (Phase 1, unchanged), plus RQ1/DT1 SysEx read/write (M3) — reads are queued and serviced
  one at a time (the reply-address bug means concurrent requests can't be told apart), writes are
  fire-and-forget.
- `src/midi/SysExCodec.h` — pure RQ1/DT1 message building, checksum, and uint7/nibble2/nibble4/ascii
  encode/decode. No MIDI I/O.
- `src/model/ZenCoreAddressing.{h,cpp}` — the 7-bit-carry tone-slot addressing formula (C++ port of
  `tools/zencore_addressing.py`).
- `src/model/ParameterMap.{h,cpp}` — loads `data/zencore-map.json` at runtime, exposes field
  lookups by block+name, plus every-field-in-a-block and per-partial-or-not lookups used by
  ToneSnapshot's chunk plan. The one place in `/src` that reads the map.
- `src/model/ToneSnapshot.{h,cpp}` — bulk (whole-tone) read/write and JSON (de)serialization,
  described above. Pure data + a paced `CCTransmitter`-driven writer, no UI.

Nothing here is macOS- or Windows-specific — it's plain cross-platform JUCE (`juce::MidiOutput`),
so it should build and run unmodified with the commands below.

## Why we're on Windows

Built and ran the app fine on macOS, but hit a **macOS 26.6 ("Tahoe") OS bug**, confirmed and not
fixable from our side:

- `usbaudiod` (macOS's USB audio class daemon) creates audio streaming interfaces correctly for a
  USB Audio Class device, but never creates a MIDI streaming interface for one — verified via
  system logs (`log show`) on **two unrelated vendors**: a Roland MC-707 (in both VENDOR and
  GENERIC USB driver modes) and a Focusrite Scarlett 8i6. Same failure signature both times, at
  the OS level, before any driver or DIN cabling comes into play.
- Ruled out: reinstalling/checking Roland's driver (1.0.3 is already their latest, targets Sonoma
  14.x+, predates Tahoe), restarting the CoreMIDI server, switching USB Driver mode on the device,
  routing MIDI through the Scarlett's physical DIN ports instead of USB-MIDI class.
- Matches a community-reported issue: [MIDI to USB under macOS
  Tahoe](https://forums.steinberg.net/t/midi-to-usb-under-macos-tahoe/1027181) — not Roland- or
  MC-707-specific.
- No macOS update was available to fix it as of 2026-08-30.

Windows has its own, separate MIDI stack unaffected by this, hence moving here to actually verify
Phase 1 against the real device.

## Build on Windows

Prerequisites: Git for Windows, CMake, Visual Studio 2022 (Community is fine) with "Desktop
development with C++".

```bash
git clone https://github.com/johnnytulloch/mc707-editor.git
cd mc707-editor
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug --target MC707Editor
```

First configure fetches JUCE (~a few hundred MB via git) — needs internet access. Built app lands
at `build\MC707Editor_artefacts\Debug\MC-707 Editor.exe`.

## What "done" looks like (verified, both Phase 1 and the control panel)

1. Connect the MC-707 over USB (either USB Driver mode should work on Windows — GENERIC needs no
   driver install, VENDOR uses Roland's Windows driver for the fuller audio interface).
2. Launch the app, hit Refresh, the MC-707 should appear in the MIDI Output dropdown (this is the
   thing that never happened on macOS).
3. Select it. Each track strip (1–8) sends on the matching fixed MIDI channel (Track N → channel
   N) — turn a knob on the track whose channel matches whatever part is listening on the device;
   the sound should audibly change. Confirmed working end-to-end.
4. If a knob move does *not* change the sound but the device does appear in the dropdown: check
   MIDI channel matches the receiving track/part on the MC-707 first (most likely cause) before
   assuming anything else is wrong.

## Conventions carried over from the brief (`docs/BRIEF.md` §10)

- C++20, JUCE, CMake — not Projucer.
- SysEx layer (Phase 2+) must be unit-testable without hardware — mock the transport.
- Every device write goes through one logging chokepoint.
- Parameter map is data (`data/zencore-map.json`), never a hardcoded address in `/src`.
- Never write SysEx to a project the user cares about during development — scratch project only.
