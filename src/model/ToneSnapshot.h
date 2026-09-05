#pragma once

#include <juce_core/juce_core.h>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>
#include "ParameterMap.h"

class CCTransmitter;

// A captured copy of everything data/zencore-map.json currently maps for one
// tone slot (track sound or a specific clip) -- the building block for
// "write this tone elsewhere" and "save/load a tone to a local file". Does
// NOT cover fields not yet mapped (PCM wave/sample, keyboard/velocity range,
// full MFX type parameters, ...) -- see tools/FINDINGS.md's
// missing-functionality scan.
struct ToneSnapshot
{
    // One block-instance's captured bytes, spanning from its lowest mapped
    // field's offset to its highest mapped field's offset+size. Bytes inside
    // that span not individually named by a field are still captured and
    // written back verbatim -- safe, since they're real device bytes just
    // read, never a synthesized value for an unknown byte.
    struct BlockChunk
    {
        juce::String blockId;
        int partialIdx = -1; // -1 = not a per-partial block (e.g. tone_common)
        int spanOffset = 0;  // relative to the block's own base
        std::vector<uint8_t> bytes;
    };

    juce::String name;  // decoded from tone_common's "Name" field
    int category = 0;   // decoded from tone_common's "Category" field
    std::vector<BlockChunk> chunks;
};

juce::var toneSnapshotToVar (const ToneSnapshot& snapshot);
std::optional<ToneSnapshot> toneSnapshotFromVar (const juce::var& v);

// One block-instance to read/write for a full tone: which block, which
// partial (-1 if not per-partial), and the mapped-field span within it.
struct ChunkPlanEntry
{
    juce::String blockId;
    int partialIdx = -1;
    int spanOffset = 0;
    int spanSize = 0;
};

// The full list of chunks that make up one tone, derived from ParameterMap
// alone -- the same list is used for every read and every write.
std::vector<ChunkPlanEntry> computeChunkPlan (const ParameterMap& map);

// Reads every chunk in the plan, based at slotBase (a full slot address from
// ZenCoreAddressing::getSlotAddress -- the block-relative-offset and
// spanOffset additions on top of it are plain addition, proven safe
// elsewhere in this codebase, see ToneEditComponent::computeAddress). Fires
// callback once: success=false (with an empty snapshot) if any chunk's read
// fails or times out, rather than returning a partial capture.
using SnapshotReadCallback = std::function<void (bool success, ToneSnapshot snapshot)>;
void readToneSnapshot (CCTransmitter& transmitter, const ParameterMap& map,
                        uint32_t slotBase, SnapshotReadCallback callback);

// Writes every captured chunk to slotBase. Fire-and-forget per chunk
// (matches CCTransmitter::sendSysExWrite -- no per-write read-back in this
// pilot); callback fires once every write has been sent.
using SnapshotWriteCallback = std::function<void (bool success)>;
void writeToneSnapshot (CCTransmitter& transmitter, const ParameterMap& map,
                         uint32_t slotBase, const ToneSnapshot& snapshot, SnapshotWriteCallback callback);
