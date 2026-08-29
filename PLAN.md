# MC-707 Editor — Implementation Plan

Source brief: `CLAUDE_2.md` (project brief, kept verbatim in this repo as `docs/BRIEF.md`).

## Evaluation of the brief

The brief is solid and unusually disciplined about separating verified fact from hypothesis —
keep that discipline as the project grows; don't let §4's unverified items quietly become
assumptions in code.

Two things worth calling out before writing more code:

1. **The whole real-time SysEx path hinges on the M1 gate, which is unverified.** Address-map
   accuracy, checksum behaviour, and whether DT1 writes are even honoured by current firmware are
   all open questions. Nothing in M2 onward should be built until M1 passes on real hardware.

2. **The four parameters requested for the first editor — cutoff, resonance, attack, release —
   are exactly the four parameters with documented, reliable CC numbers** (CC74, CC71, CC73,
   CC72, per brief §3 "CC/NRPN fallback"). That means a genuinely useful real-time editor for
   these four can be built *today*, with zero dependency on the SysEx address map, zero risk of
   corrupting a patch (CC can't brick anything the way a bad DT1 write theoretically could), and
   no need to wait on the M0/M1 discovery work. This is the natural Phase 1 — it's also exactly
   what was asked for.

   Caveat: CC editing is not equivalent to the eventual SysEx editor — it's a live performance
   knob, not a stored-patch read/write. It won't read back a patch's current cutoff value (the
   device doesn't expose that over CC), and it affects whichever track/channel is listening. Good
   enough for "adjust these four live," not a substitute for M3.

3. **No MC-707 is currently connected to this Mac** (checked via `system_profiler`), and neither
   `cmake` nor JUCE are installed yet. Phase 1 can be built and its UI verified standalone, but
   sending real CC and confirming the sound changes needs the device plugged in over USB with
   Storage Mode *off* (see brief §3, USB file transfer section — MIDI and mass-storage are
   mutually exclusive sessions).

Platform decision (standalone JUCE, C++20, CMake not Projucer) is sound as argued — agreed,
no changes.

## Restructured phases

The brief's own M0–M5 are kept, but a CC-based Phase 1 is inserted ahead of them since it needs
none of the SysEx groundwork and delivers the specific thing asked for immediately.

| Phase | Maps to brief | Deliverable | Gate to pass |
|-------|--------------|-------------|--------------|
| **1. CC Quick-Edit** | (new, ahead of M0) | Standalone JUCE app: MIDI output + channel picker, 4 sliders (Cutoff/CC74, Resonance/CC71, Attack/CC73, Release/CC72), sends CC live. | Builds and runs; slider move visibly changes sound on real hardware. |
| **2. Discovery harness** | M0 | Python scratch tooling (`/tools`): send arbitrary SysEx, log replies. Identity Request round-trip. | Can send/receive raw SysEx and log it. |
| **3. Viability gate** | M1 | Identity confirms model ID `00 00 00 5D`; RQ1 a tone descriptor; correct the buggy reply address; DT1-write one safe parameter and confirm audible change; full read→modify→write→re-read round-trip. | **Hard stop if this fails** — reassess per brief §9 fallback plan before any further app code. |
| **4. Parameter map** | M2 | `data/zencore-map.json`, derived from Jupiter-X/Xm map via `sysex-mapatron`, validated byte-by-byte on real MC-707. | Every offset used by the editor has been confirmed by write+listen. |
| **5. SysEx editor core** | M3 | `MC707Device`, `SysExCodec` (with address-bug correction), transport queue, tone selection (136 slots), dirty tracking, undo. Replaces Phase 1's CC layer with real patch read/write for cutoff/resonance/attack/release, then expands. | Round-trip tested against scratch project only. |
| **6. Full UI** | M4 | Partial selector (1–4) with link toggle, grouped sections (Osc/Filter/Amp/Pitch/LFO/Envelopes), live envelope graphic, MFX tab. | Matches brief §8 UI direction — one page, no menu-diving. |
| **7. File features** | M5 | `.SVZ` import/export (via JDTools reference), Storage Mode copy helper with safe-eject. | SVZ round-trip verified; Storage Mode instructions surfaced in UI. |

Fallback (brief §9) stays in force: if Phase 3 fails, pivot to offline `.SVZ` design + CC/Matrix
Control for live tweaking, and stop building the real-time SysEx path until Roland changes
something.

## This session's scope

Building **Phase 1** now: JUCE standalone app, CMake (FetchContent for JUCE, no Projucer), one
window, MIDI output device selector, channel selector (1–16), four labelled sliders sending
CC74/71/73/72 on change, throttled to avoid flooding the port. No SysEx code yet — that starts at
Phase 2, gated by Phase 3 passing on real hardware.
