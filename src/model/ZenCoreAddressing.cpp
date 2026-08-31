#include "ZenCoreAddressing.h"
#include <juce_core/juce_core.h>

namespace ZenCoreAddressing
{
    uint32_t add7BitCarry (uint32_t base, uint32_t offset)
    {
        uint8_t b[4] = {
            (uint8_t) ((base >> 24) & 0xFF), (uint8_t) ((base >> 16) & 0xFF),
            (uint8_t) ((base >> 8) & 0xFF), (uint8_t) (base & 0xFF)
        };
        uint8_t o[4] = {
            (uint8_t) ((offset >> 24) & 0xFF), (uint8_t) ((offset >> 16) & 0xFF),
            (uint8_t) ((offset >> 8) & 0xFF), (uint8_t) (offset & 0xFF)
        };

        uint8_t result[4] = { 0, 0, 0, 0 };
        int carry = 0;
        for (int i = 3; i >= 0; --i)
        {
            int total = b[i] + o[i] + carry;
            carry = total / 0x80;
            result[i] = (uint8_t) (total % 0x80);
        }

        return ((uint32_t) result[0] << 24) | ((uint32_t) result[1] << 16)
             | ((uint32_t) result[2] << 8) | (uint32_t) result[3];
    }

    uint32_t getSlotAddress (const std::array<uint32_t, 8>& trackBase, uint32_t toneAlignment,
                              int trackIdx, std::optional<int> clipIdx)
    {
        constexpr int clipsPerTrack = 16;

        trackIdx = juce::jlimit (0, 7, trackIdx);
        int n = clipIdx.has_value() ? juce::jlimit (0, clipsPerTrack - 1, *clipIdx) : clipsPerTrack;

        return add7BitCarry (trackBase[(size_t) trackIdx], toneAlignment * (uint32_t) n);
    }
}
