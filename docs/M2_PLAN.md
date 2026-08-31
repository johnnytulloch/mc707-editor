# M2 — Parameter Map: Scope & Approach

Scoping only — no code written yet, no further hardware writes performed. Written after M1
(viability gate) passed on real hardware; see `tools/FINDINGS.md` for that session's raw data.

## Goal / deliverable

`data/zencore-map.json` — address, size, range, name, display, group for every editable ZEN-Core
tone parameter (brief §6), validated against the real MC-707, not just derived on paper. Per
`PLAN.md`'s own milestone table, this is expected to be the longest phase — scope accordingly
rather than trying to cover the whole engine (MFX especially) in one pass.

## Two distinct sub-problems

M1's discovery work revealed these are genuinely separate, and the second one is **not** covered
by the Jupiter-X/Xm reference at all:

### A. Tone-slot addressing — which base address is which (track, clip)

What we know (from `tools/FINDINGS.md`): tones are spaced roughly `0x00020000` apart within a
region, but a naive `base + step*n` scan **overflowed past n=63** — a byte exceeded `0x7F`. The
real scheme is very likely a proper nibble/7-bit-per-byte address (track and clip indices packed
into specific bits, with carries between bytes), not flat linear spacing across the whole range.
Manually scanning further would be slow and error-prone.

**Plan:** before more scanning, read `Locriana/mcpoker`'s (and its successor `mctools_basic`'s)
actual source — brief §3 says it already does "address-space scan, descriptor location," i.e. this
exact problem, and it's public domain. Porting its logic should be much faster than re-deriving the
indexing formula from scratch. Once the formula is confirmed, replace the ad hoc
`tools/scan_names.py` with a proper `tools/discover_tone_slots.py` that walks all 136 real slots
(8 track-tones + 128 clip-tones) and records (track, clip-or-none, address, name) for every one.

### B. Within-tone field layout — offsets for filter/amp/envelope/LFO/partials

What we know: name field is `+0x00` (16 bytes); `+0x15` = **Level, confirmed** (audible write +
round trip); `+0x16` = `0x40` in both tones tested, likely Pan (not yet independently confirmed).
Brief §3's block table gives coarse starts from the official Jupiter-X/Xm implementation (Tone
Common `000000`, MFX `000100`, PMT `001000`, Partials `002000`–`002300`, Filter Env `002800`+, Amp
Env `002C00`+, LFO `003000`+, EQ `003800`+) — these are relative offsets *within one tone*, and
should carry over since Jupiter-X/Xm documents the same ZEN-Core engine (brief §3).

**Plan:** fetch the official Jupiter-X/Xm MIDI Implementation, run `asutherland/sysex-mapatron`
(Apache-2.0) against it to derive a fine-grained field map, then merge with the MC-707-specific
corrections already found (Level, and whatever else validation turns up) into the final JSON.

## Validation strategy

- **Structural pass (automated, every entry):** RQ1-read each declared address, confirm a reply
  comes back and the raw value is plausible for its declared range. Cheap, catches gross
  address/offset errors early, no audible check needed.
- **Audible pass (manual, priority subset only):** write → listen → round-trip → restore, the exact
  technique from tonight's `tools/dt1_write_test.py` session. Priority order per brief §3's
  "genuinely useful synth editor" bar: filter cutoff/resonance, amp level/pan, envelope stages, LFO
  rate/depth, each of the 4 partials' core params. **MFX (93 effect types) explicitly deferred** —
  brief says it "can come later."
- Every map entry records how it was validated (`structural` vs `audible-confirmed`) so M3's app
  code and future-you can tell a solid offset from a plausible-but-unverified one.

## New tooling this phase would need (not built yet)

- `tools/discover_tone_slots.py` — replaces `scan_names.py` once the real addressing formula (from
  mcpoker/mctools_basic) is understood; walks all 136 slots properly.
- A script to run `sysex-mapatron` against the Jupiter-X/Xm doc and emit a draft field map.
- `tools/build_map.py` — merges the sysex-mapatron draft + MC-707-specific corrections + validation
  results into `data/zencore-map.json`, matching the brief §6 schema.
- `tools/validate_map.py` — automates the structural pass over the whole map; interactive mode for
  the audible pass, reusing the confirm/restore flow already proven in `dt1_write_test.py`.

## Risks / open questions flagged for this phase

- The address-arithmetic overflow past n=63 needs a real model (from prior art) before any more
  systematic slot discovery — brute-force scanning further would likely just overflow again.
- Community NRPN map "completeness" is still unverified (brief §4) — not a blocker for M2, worth a
  later cross-check.
- Keep scope realistic: Tone Common + Filter + Amp + Envelopes + the 4 partials' core params first;
  MFX and PMT (93 effect types) are a later pass, not this one.
