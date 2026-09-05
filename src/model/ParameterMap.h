#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <optional>
#include <vector>

// One field's descriptor, as looked up from data/zencore-map.json.
struct FieldDescriptor
{
    juce::String name;
    int offsetWithinBlock = 0; // relative to the block's own base
    int size = 0;              // raw bytes
    juce::String encoding;     // "uint7" | "nibble2" | "nibble4" | "ascii"
    int rangeLow = 0;
    int rangeHigh = 0;
    juce::String group;
    juce::String validation;
};

// Loads data/zencore-map.json (embedded via BinaryData, see CMakeLists.txt)
// once and exposes field lookups + addressing metadata. See CLAUDE.md:
// "Parameter map is data, never a hardcoded address in /src" -- this is the
// one place in the app that reads it.
class ParameterMap
{
public:
    ParameterMap();

    // blockId e.g. "tone_partial", "partial_amp_env". fieldName must match
    // the JSON's "name" field exactly (e.g. "Pan", "TVA Env Time1").
    std::optional<FieldDescriptor> findField (const juce::String& blockId, const juce::String& fieldName) const;

    // Every field declared under blocks[blockId].fields, in declaration
    // order. Used to compute a block's mapped-field span for bulk read/write
    // (ToneSnapshot) rather than looking up one field at a time.
    std::vector<FieldDescriptor> allFields (const juce::String& blockId) const;

    // Every key under "blocks" (e.g. "tone_common", "tone_partial", ...).
    juce::StringArray blockIds() const;

    // True if meta.block_relative_offsets[blockId] is an array (one entry
    // per partial 1-4) rather than a single fixed-offset string.
    bool blockIsPerPartial (const juce::String& blockId) const;

    // Relative offset of a block for a given partial index (0-3). Blocks
    // with a single relative_offset (e.g. tone_common) ignore partialIdx.
    int blockRelativeOffset (const juce::String& blockId, int partialIdx) const;

    const std::array<uint32_t, 8>& trackBase() const { return trackBaseTable; }
    uint32_t toneAlignment() const { return toneAlignmentValue; }

    bool isValid() const { return loaded; }

private:
    juce::var root;
    bool loaded = false;
    std::array<uint32_t, 8> trackBaseTable {};
    uint32_t toneAlignmentValue = 0;
};
