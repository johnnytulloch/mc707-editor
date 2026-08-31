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

**The main UI has been rebuilt as a full 8-channel control-surface panel**, matching a supplied
mockup of the MC-707's mixer-style layout, built and confirmed working against real hardware. This
supersedes Phase 1's single 4-slider view and is the shell later "tone editing" work will hang off
of (not started yet). Still CC/Program-Change only — no SysEx sent from the app.

- `CMakeLists.txt` — JUCE via CMake FetchContent (JUCE tag 8.0.6), builds a standalone GUI app
  target `MC707Editor`.
- `src/Main.cpp` — JUCE application/window boilerplate.
- `src/ui/MainComponent.{h,cpp}` — app shell: MIDI output device picker, 8 `TrackStripComponent`s,
  an LCD-style 2-line readout showing the last-touched param/value, a Scene (1–8) selector. One
  shared 50Hz timer polls all 8 strips and sends only on change (Phase 1's throttling pattern).
- `src/ui/TrackStripComponent.{h,cpp}` — one mixer channel strip (Filter/Mod/Porta/Vibrato/FX knob
  groups, Mute, Fader, Pan, Clip number). Full CC mapping table and the two behavioral calls made
  (Mute = CC7 pseudo-mute, Clip = Program Change, Scene = interactive but not yet wired — no
  documented mapping exists) are in `PLAN.md`. Two knobs (Porta Depth, Delay Send) are visible but
  disabled — no documented CC, built honestly rather than guessed.
- `src/midi/CCTransmitter.{h,cpp}` — thin wrapper over `juce::MidiOutput`; sends CC/Program Change
  on an explicit channel per call (no longer single-channel like Phase 1's version). No SysEx here
  — this is CC-only, which is why it needed none of the brief's M0/M1 discovery work to be useful
  immediately.

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
