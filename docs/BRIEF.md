# MC-707 ZEN-Core Patch Editor

A clean, uncluttered desktop editor for Roland MC-707 tone/patch parameters, plus a helper for
copying samples and MIDI files to the device's SD card.

This file is the project brief. Read it fully before starting work. It contains verified facts,
unverified assumptions, and hard safety rules. Do not treat the unverified section as fact.

---

## 1. Goal

Build a standalone desktop application that edits ZEN-Core tones on a Roland MC-707 over USB MIDI
SysEx, with a deliberately minimal, well-organised UI.

The motivating problem: the only existing full-featured editor (ben67 ZEN Editor, closed source,
~€30) works but has a dense, dated layout. The hardware itself buries synth parameters behind
menu-diving. We want fewer things on screen at once, sensible grouping, and no menu archaeology.

Secondary goal: a file-transfer helper for WAV samples and MIDI phrase files to the SD card.

### Non-goals (for v1)
- Pattern/clip/sequencer editing
- Project (`.mpj`) management
- Audio recording or playback
- Supporting other ZEN-Core devices (MC-101, Fantom, Jupiter-X) — design so it's possible later,
  but do not build it now
- Roland Cloud `.SDZ` packs (encrypted, license-locked, out of scope)

---

## 2. Platform decision

**Standalone JUCE application (C++), Windows + macOS.**

Rationale, briefly, so this doesn't get relitigated:
- JUCE `MidiInput`/`MidiOutput` open the MC-707's USB-MIDI port directly. No host in the way.
- Ableton Live filters SysEx out of the track MIDI stream, and Max for Live devices only see the
  MIDI routed to their own track. Workarounds exist (David Butler's `imp.midi` externals) but add
  fragility — Windows port contention, Apple Silicon loading issues, historical `midiout` size caps.
- Hosted VST3/AU has the same port-access problem, and VST3 MIDI plumbing is inconsistent across hosts.
- We need ordinary filesystem access for `.SVZ` and sample transfer. A standalone app just has it.

JUCE builds standalone and plugin targets from one project. If a plugin build is ever wanted, it
should still open the MC-707's MIDI port itself rather than route through the host.

---

## 3. Technical background — VERIFIED

These are established from working third-party tools and official Roland docs for sibling devices.

### The device does speak SysEx, undocumented
Roland published only a MIDI Implementation *Chart* for the MC-707
(`MC-707_MIDIImpleChart_eng03_W.pdf`, Sep 2020, v1.60) — channel messages only, no address map.
The community long assumed there was no SysEx. That is wrong.

- **Model ID: `00 00 00 5D`** (MC-101 is `00 00 00 5E`)
- Responds to standard Roland **RQ1 (`0x11`)** = read, **DT1 (`0x12`)** = write
- Standard Roland checksum: `checksum = (128 - (sum of address+data bytes mod 128)) mod 128`
- Known-working DT1 from Locriana's `mcpoker`:
  `F0 41 10 00 00 00 5D 12 30 42 00 18 42 34 F7`

### The reply-address bug (important)
On MC-707/MC-101, DT1 replies carry the **correct payload but a wrong address field**. Example
captured on MC-101:

```
Request:  F0 41 10 00 00 00 5E 11 30 00 00 00 00 00 00 10 00 F7
Reply:    F0 41 10 00 00 00 5E 12 06 00 00 00 20 20 20 ... 7A F7
                                   ^^ address is wrong (expected 30 00 00 00)
                                              ^^ 0x20 = ASCII space, name padding
```

Any correct implementation must re-map returned addresses before storing. Build this into the
SysEx layer from day one, not as a patch later.

### Address space
- Tone descriptors begin around **`0x30000000`**
- MC-707 exposes 8 tracks × 16 clip tones = 128 clip-tone descriptors, plus 1 track-tone descriptor
  per track = **136 editable ZEN-Core tones per project** (matches ben67's stated feature set)

### ZEN-Core tone block layout
Taken from the **official Jupiter-X/Xm MIDI Implementation** (model ID `00 00 00 65`), which
documents the same engine. Offsets within one ZCore tone:

| Offset      | Block |
|-------------|-------|
| `00 00 00`  | Tone Common — **the patch-level block we care about** |
| `00 01 00`  | Tone MFX |
| `00 10 00`  | Tone PMT (partial mix table, tone-level) |
| `00 20 00`–`00 23 00` | Partials 1–4 |
| `00 28 00`+ | Partial Filter Env |
| `00 2C 00`+ | Partial Amp Env |
| `00 30 00`+ | Partial LFO |
| `00 38 00`+ | Partial EQ |

User tones are spaced `0x00020000` apart.

### Architectural note on "patch-level only"
The original scope was "main synth patch, not partials." Be aware: on ZEN-Core, **filter, amp,
envelopes and LFOs all live at the partial level.** A strictly no-partial editor exposes only Tone
Common + MFX + PMT, which is not a useful synth editor.

**Decision for this project:** expose partials, but present them cleanly — a partial selector (1–4)
with a link/parallel-edit toggle, rather than four columns of everything at once. This is the single
biggest clutter reducer versus the ben67 layout.

### The MC-707 does not transmit its own edits
Unlike the Fantom (which has a system option to emit SysEx on parameter change), the MC-707 does
**not** reliably send SysEx or even CC when you tweak on the hardware. The usual reverse-engineering
workflow of "move a knob, capture what comes out" **does not work here.**

Discovery must be **request/response probing**: send RQ1, diff DT1 replies, write and listen.
Plan tooling around this.

### CC / NRPN fallback
Received CCs (not a full editing surface, but reliable):
CC1 mod, CC5 portamento time, CC7 volume, CC10 pan, CC11 expression, CC71 resonance, CC72 release,
CC73 attack, CC74 cutoff, CC75 decay, CC76/77/78 vibrato rate/depth/delay, CC80–83 the
FILTER/MOD/FX/SOUND knobs, CC91 reverb send, CC92 chorus send. Plus Matrix Control: 4 assignable
CC → partial-parameter routings per track.

An NRPN map is catalogued at midi.guide for the MC-707. Useful as a fallback path if SysEx writes
prove unreliable.

### USB file transfer — Storage Mode
- With USB **disconnected**: hold `[SHIFT]` + `[KNOB ASSIGN]` → UTILITY → **STORAGE MODE** →
  `[ENTER]`, then connect USB. SD card mounts as class-compliant USB mass storage.
- **The device does nothing else in Storage Mode.** File transfer and MIDI editing are mutually
  exclusive sessions. The UI must make this explicit.
- Card must be formatted by the MC-707 first (FAT).
- Directory structure: `ROLAND/GROOVEBOX/` → `PROJECT/` (`.mpj`), `SAMPLE/EXPORT/`, `MIDI/`
- Sample formats: user samples WAV 32–96 kHz / 16- or 24-bit, mono or stereo; total 12 min mono or
  6 min stereo at 44.1 kHz. Looper clips: WAV 44.1 kHz, 16/24/32-bit or 32-bit float, ~60 s.
  Mono recommended for tone/drum partials.
- **Copying files is not importing them.** Assigning a sample to a tone partial or drum pad must be
  done on the device via the Sample Browser. There is no remote trigger for this. The app can only
  place files and instruct.
- Always trigger an OS-level safe eject before the user disconnects.

### Prior art
- **`Locriana/mcpoker`** and its successor `mctools_basic` (GitHub, **Unlicense / public domain**) —
  Python PoC. Raw SysEx send, RQ1, DT1 parse, address-space scan, descriptor location. This is the
  most directly reusable reference. Public domain, so we can freely learn from and port it.
- **`asutherland/sysex-mapatron`** (Apache-2.0, Python + Rust) — derives ZEN-Core SysEx maps for the
  *documented* Jupiter-Xm and Fantom. Its map-derivation tooling is useful for generating our
  reference map. (Its README says the MC-707 has no SysEx — written before the `5D` discovery.)
- **`sagamusix/JDTools`** — reverse-engineered `.SVZ` / `.BIN` / `.SVD` ZEN-Core patch formats.
  The reference if we add `.SVZ` import/export.
- **ben67 ZEN Editor** (benis.it) — closed source, commercial. Study its *feature list* as a
  completeness checklist. **Do not attempt to decompile, extract, or reuse its data.**
- **`ricardofeynman/Awesome-MC-707`** — curated link hub.

---

## 4. Technical background — UNVERIFIED

Treat as hypotheses. Verify empirically before building on them. Do not let these silently become
assumptions in the code.

- The MC-707 identity reply hex has not been independently captured (only the MC-101's). The `5D`
  model ID is inferred from working DT1 traffic. **Verify first thing.**
- Whether the MC-707 accepts `00` as a checksum (proven on Jupiter-X/Xm, unproven here).
- Exact per-block byte sizes and offsets on the MC-707. The Jupiter-X map is a *starting reference*,
  not ground truth. Validate every offset by writing and listening.
- Completeness of the community NRPN map.
- Reports of a "missing frame" issue on imported WAVs. Test if we build sample transfer.

---

## 5. Hard safety rules

- **Never write SysEx to a project the user cares about during development.** Every write path must
  be tested against a scratch project. The app should warn on first run.
- Roland has previously *removed* ZEN-Core SysEx capability in firmware updates (documented on
  Fantom firmware 2.5). Assume this back door can vanish. Fail gracefully; never assume a write
  succeeded without reading back.
- The SysEx path is undocumented and unofficial. Surface this to the user in the UI — bad writes can
  corrupt a project.
- Do not decompile or extract data from ben67's editor.

---

## 6. Architecture

```
/src
  /midi          MC707Device: port discovery, open/close, identity handshake
                 SysEx codec: RQ1/DT1 build + parse, checksum, ADDRESS BUG CORRECTION
                 Transport queue: throttling, timeouts, retries
  /model         ParameterMap: loaded from data/, not hardcoded
                 Tone: in-memory tone state, dirty tracking, undo
  /ui            Main editor, section panels, partial selector
  /files         Storage Mode helper: volume detection, copy, safe-eject
  /probe         Dev-only: address scanner, diff tool, map validator
/data
  zencore-map.json    parameter map: address, size, range, name, display, group
/tools
  (Python scratch scripts for discovery, ported from mcpoker patterns)
```

**The parameter map is data, not code.** JSON in `/data`, loaded at runtime. The UI should be
generated from it where practical. This is what makes supporting MC-101/Fantom later cheap, and
what makes correcting a wrong offset a one-line data change rather than a rebuild.

---

## 7. Milestones

### M0 — Discovery harness (before app code)
Python scratch scripts, or a minimal JUCE console target. Also set up a SysEx monitor:
SysEx Librarian / MIDI Monitor (macOS), MIDI-OX (Windows), or Bome SendSX / Protokol.

Deliverable: can send arbitrary SysEx to the MC-707 and log replies.

### M1 — Prove the whole thing is viable
This is the gate. If any of these fail, stop and reassess before writing app code.

1. Send Identity Request `F0 7E 7F 06 01 F7`; confirm reply and model ID `00 00 00 5D`.
2. RQ1 a single tone descriptor near `0x30000000`; parse the DT1 reply and **correct the buggy
   address**.
3. DT1 write one audible, safe parameter (Tone Common level, or a partial cutoff); confirm the
   sound changes.
4. Round-trip: read full tone → modify one byte → write back → re-read and confirm.

If step 3 fails on current firmware, first validate the signal chain via CC (e.g. CC74 cutoff) to
confirm the connection is fine, then revisit SysEx. If SysEx writes are genuinely unreliable, see
the fallback plan in §9.

### M2 — Build and validate the parameter map
Use `sysex-mapatron` against the Jupiter-X/Xm official MIDI Implementation to generate a ZCore map,
then port and **validate each offset on the MC-707** by writing and listening. Cross-check against
JDTools' `.SVZ` structure. Output: `data/zencore-map.json`.

Expect this to be the longest phase. Prioritise Tone Common, filter, amp, envelopes and the four
partials' core parameters. MFX (93 effect types) can come later.

### M3 — JUCE app: editor core
Port the SysEx layer to C++. Device connection UI, tone selection (136 slots), read tone, edit,
write, dirty-state tracking, undo.

### M4 — UI
See §8.

### M5 — File features
- `.SVZ` import/export (leveraging JDTools' format work) — a robust save/load path that avoids the
  SysEx address bug entirely.
- Storage Mode file copy helper for WAV and MIDI, with explicit on-device import instructions.

---

## 8. UI direction

The whole point of this project. Keep it disciplined.

- **One main page.** Grouped, labelled sections: Oscillator/Tone type, Filter, Amp, Pitch, LFO,
  Envelopes. Effects/MFX on a separate tab.
- **Partial selector (1–4) with a link toggle** for parallel editing. Never show all four partials'
  full parameter sets simultaneously — that is precisely the ben67 problem.
- **Graphic envelope display**, live-updating. ben67 has this and it is genuinely useful; keep it.
- Resizable, vector/scaled UI.
- Design goal: everything important for one tone visible without menu-diving.
- Read `/mnt/skills/public/frontend-design/SKILL.md` before doing visual design work, if working in
  an environment where that's available.

---

## 9. Fallback plan

If M1 step 3 cannot be made reliable on current firmware, pivot to an **offline `.SVZ` editor**:
design a tone in the app → export `.SVZ` → user loads it via SD card. Combine with CC and Matrix
Control for live tweaking. Still genuinely valuable, just without real-time immediacy.

Revisit real-time editing if Roland ever publishes an official MC-707 MIDI Implementation or adds
parameter-change transmission.

---

## 10. Conventions

- C++20, JUCE. CMake, not Projucer.
- SysEx layer must be unit-testable without hardware — mock the transport.
- All device writes go through one chokepoint that logs the raw bytes. Non-negotiable for debugging.
- Parameter map changes are data changes; never hardcode an address in `/src`.
- Commit the discovery scripts in `/tools`. Future-you will need them.
