#include "ParameterMap.h"
#include "BinaryData.h"

namespace
{
    uint32_t parseHexAddress (const juce::String& s)
    {
        return (uint32_t) s.trim().getHexValue64();
    }
}

ParameterMap::ParameterMap()
{
    juce::String jsonText (BinaryData::zencoremap_json, (size_t) BinaryData::zencoremap_jsonSize);
    root = juce::JSON::parse (jsonText);
    if (root.isVoid())
        return;

    auto slotAddressing = root.getProperty ("meta", {}).getProperty ("slot_addressing", {});
    if (auto* arr = slotAddressing.getProperty ("track_base", {}).getArray())
    {
        for (int i = 0; i < juce::jmin (8, arr->size()); ++i)
            trackBaseTable[(size_t) i] = parseHexAddress ((*arr)[i].toString());
    }
    toneAlignmentValue = parseHexAddress (slotAddressing.getProperty ("tone_alignment", {}).toString());

    loaded = true;
}

int ParameterMap::blockRelativeOffset (const juce::String& blockId, int partialIdx) const
{
    auto offsetsHolder = root.getProperty ("meta", {}).getProperty ("block_relative_offsets", {});
    auto offsets = offsetsHolder.getProperty (blockId, {});

    // A block name that exists under "blocks" but has no matching entry
    // here (e.g. a typo, or the two getting renamed out of sync) would
    // otherwise silently resolve to offset 0 -- a wrong-but-plausible
    // address that's easy to miss until a field reads/writes garbage. Hit
    // this exact bug once already (blocks.tone_synth_pmt vs this table's
    // "synth_pmt", no "tone_" prefix) -- fail loudly instead next time.
    jassert (offsets.isString() || offsets.isArray());

    if (auto* arr = offsets.getArray())
    {
        auto idx = juce::jlimit (0, arr->size() - 1, partialIdx);
        return (int) parseHexAddress ((*arr)[idx].toString());
    }
    return (int) parseHexAddress (offsets.toString());
}

std::optional<FieldDescriptor> ParameterMap::findField (const juce::String& blockId, const juce::String& fieldName) const
{
    auto fields = root.getProperty ("blocks", {}).getProperty (blockId, {}).getProperty ("fields", {});
    if (auto* arr = fields.getArray())
    {
        for (auto& f : *arr)
        {
            if (f.getProperty ("name", {}).toString() != fieldName)
                continue;

            FieldDescriptor d;
            d.name = f.getProperty ("name", {}).toString();
            d.offsetWithinBlock = (int) parseHexAddress (f.getProperty ("offset", {}).toString());
            d.size = (int) f.getProperty ("size", {});
            d.encoding = f.getProperty ("encoding", {}).toString();
            if (auto* r = f.getProperty ("range", {}).getArray(); r != nullptr && r->size() >= 2)
            {
                d.rangeLow = (int) (*r)[0];
                d.rangeHigh = (int) (*r)[1];
            }
            d.group = f.getProperty ("group", {}).toString();
            d.validation = f.getProperty ("validation", {}).toString();
            return d;
        }
    }
    return std::nullopt;
}
