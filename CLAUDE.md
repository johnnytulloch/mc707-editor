# MC-707 Editor — Project Context for Claude Code

Read `PLAN.md` (phased implementation plan + evaluation of the brief) and `docs/BRIEF.md` (original
project brief) in full before doing anything else. This file is a handoff note on top of those —
it explains what's been tried, what's blocked, and why.

## Current status

**Phase 1 (CC Quick-Edit) is built and code-complete**, but unverified against real hardware —
see "Why we're on Windows" below.

- `CMakeLists.txt` — JUCE via CMake FetchContent (JUCE tag 8.0.6), builds a standalone GUI app
  target `MC707Editor`.
- `src/Main.cpp` — JUCE application/window boilerplate.
- `src/ui/MainComponent.{h,cpp}` — four rotary sliders (Cutoff/CC74, Resonance/CC71, Attack/CC73,
  Release/CC72), a MIDI output device picker, a channel picker (1–16), 50Hz-throttled CC sending.
- `src/midi/CCTransmitter.{h,cpp}` — thin wrapper over `juce::MidiOutput`. No SysEx anywhere in
  Phase 1 — this is CC-only, which is why it needed none of the brief's M0/M1 discovery work to be
  useful immediately.

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

## What "done" looks like for Phase 1

1. Connect the MC-707 over USB (either USB Driver mode should work on Windows — GENERIC needs no
   driver install, VENDOR uses Roland's Windows driver for the fuller audio interface).
2. Launch the app, hit Refresh, the MC-707 should appear in the MIDI Output dropdown (this is the
   thing that never happened on macOS).
3. Pick it, pick a channel matching whatever track/part is listening on the device, turn a knob —
   the sound should audibly change (cutoff sweeps, resonance whistles, etc.).
4. If that works: Phase 1 is genuinely done. Report back and move to Phase 2 (`PLAN.md`) — the
   SysEx discovery harness. That phase's own convention (see brief §10) is that the SysEx layer
   must be unit-testable without hardware, so it isn't blocked by which machine you're on.
5. If a knob move does *not* change the sound but the device does appear in the dropdown: check
   MIDI channel matches the receiving track/part on the MC-707 first (most likely cause) before
   assuming anything else is wrong.

## Conventions carried over from the brief (`docs/BRIEF.md` §10)

- C++20, JUCE, CMake — not Projucer.
- SysEx layer (Phase 2+) must be unit-testable without hardware — mock the transport.
- Every device write goes through one logging chokepoint.
- Parameter map is data (`data/zencore-map.json`), never a hardcoded address in `/src`.
- Never write SysEx to a project the user cares about during development — scratch project only.
