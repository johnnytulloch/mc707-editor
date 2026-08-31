#pragma once

#include <array>
#include <cstdint>
#include <optional>

// ZEN-Core tone-slot addressing (M2 sub-problem A, tools/FINDINGS.md).
// Direct port of tools/zencore_addressing.py, validated live against real
// hardware this session (including the two tracks -- 4 and 8 -- where a
// naive plain-hex port gets the address wrong).
namespace ZenCoreAddressing
{
    // Adds base + offset treating both as 4-byte, 7-bit-clean (base-128)
    // addresses: each byte carries into the next at 128, not 256. Roland
    // SysEx address bytes are always 7-bit-clean (bit 7 = 0), so plain
    // 32-bit hex addition is wrong wherever a byte would exceed 0x7F --
    // see tools/FINDINGS.md "M2 sub-problem A solved" for the full
    // derivation and live confirmation.
    uint32_t add7BitCarry (uint32_t base, uint32_t offset);

    // trackBase: the 8 track base addresses (data/zencore-map.json's
    // meta.slot_addressing.track_base) -- passed in rather than duplicated
    // here, so the JSON stays the single source of truth (see ParameterMap).
    // trackIdx: 0-7. clipIdx: 0-15 for a clip tone, or std::nullopt for the
    // track's own "track sound" slot (one past the last clip).
    uint32_t getSlotAddress (const std::array<uint32_t, 8>& trackBase, uint32_t toneAlignment,
                              int trackIdx, std::optional<int> clipIdx);
}
