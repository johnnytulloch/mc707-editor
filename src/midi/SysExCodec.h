#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <juce_core/juce_core.h>

// Roland SysEx message building for the MC-707's undocumented RQ1/DT1
// interface. Ported from tools/mc707.py, same values already proven
// against real hardware -- see tools/FINDINGS.md. Pure logic, no MIDI I/O.
namespace SysExCodec
{
    constexpr uint8_t kRolandId = 0x41;
    constexpr uint8_t kDeviceId = 0x10; // arbitrary, matches tools/mc707.py
    constexpr std::array<uint8_t, 4> kModelId { 0x00, 0x00, 0x00, 0x5D };
    constexpr uint8_t kRq1Cmd = 0x11;
    constexpr uint8_t kDt1Cmd = 0x12;

    inline std::array<uint8_t, 4> addressBytes (uint32_t value)
    {
        return {
            (uint8_t) ((value >> 24) & 0xFF), (uint8_t) ((value >> 16) & 0xFF),
            (uint8_t) ((value >> 8) & 0xFF), (uint8_t) (value & 0xFF)
        };
    }

    // Roland checksum: (128 - (sum(address+data) mod 128)) mod 128.
    inline uint8_t checksum (const std::vector<uint8_t>& addressAndData)
    {
        int sum = 0;
        for (auto b : addressAndData)
            sum += b;
        return (uint8_t) ((128 - (sum % 128)) % 128);
    }

    // Builds the full sysex body (no F0/F7) for an RQ1 read.
    inline std::vector<uint8_t> buildRq1 (uint32_t address, uint32_t size)
    {
        auto addr = addressBytes (address);
        auto sizeBytes = addressBytes (size);

        std::vector<uint8_t> addrAndSize (addr.begin(), addr.end());
        addrAndSize.insert (addrAndSize.end(), sizeBytes.begin(), sizeBytes.end());

        std::vector<uint8_t> body { kRolandId, kDeviceId };
        body.insert (body.end(), kModelId.begin(), kModelId.end());
        body.push_back (kRq1Cmd);
        body.insert (body.end(), addrAndSize.begin(), addrAndSize.end());
        body.push_back (checksum (addrAndSize));
        return body;
    }

    // Builds the full sysex body (no F0/F7) for a DT1 write.
    inline std::vector<uint8_t> buildDt1 (uint32_t address, const std::vector<uint8_t>& data)
    {
        auto addr = addressBytes (address);

        std::vector<uint8_t> addrAndData (addr.begin(), addr.end());
        addrAndData.insert (addrAndData.end(), data.begin(), data.end());

        std::vector<uint8_t> body { kRolandId, kDeviceId };
        body.insert (body.end(), kModelId.begin(), kModelId.end());
        body.push_back (kDt1Cmd);
        body.insert (body.end(), addrAndData.begin(), addrAndData.end());
        body.push_back (checksum (addrAndData));
        return body;
    }

    // Parses a DT1 message body (no F0/F7): 41 <dev> 00 00 00 5D 12
    // <address 4B> <payload NB> <checksum 1B>.
    //
    // NOTE: the reply's own address field is documented as unreliable (the
    // "reply-address bug", tools/FINDINGS.md) -- callers must use the
    // address they *requested*, never a parsed one, to interpret the payload.
    struct ParsedDt1
    {
        bool valid = false;
        std::vector<uint8_t> data;
    };

    inline ParsedDt1 parseDt1 (const void* bytes, int length)
    {
        ParsedDt1 result;
        auto* b = static_cast<const uint8_t*> (bytes);

        // manufacturer(1) + device(1) + model(4) + cmd(1) + address(4) + checksum(1) = 12 minimum
        if (length < 12 || b[0] != kRolandId || b[6] != kDt1Cmd)
            return result;

        result.valid = true;
        result.data.assign (b + 11, b + length - 1);
        return result;
    }

    // ---- value encode/decode, per data/zencore-map.json's meta.encodings ----

    inline uint8_t encodeUint7 (int value)
    {
        if (value < 0) value = 0;
        if (value > 127) value = 127;
        return (uint8_t) value;
    }

    inline int decodeUint7 (const std::vector<uint8_t>& bytes)
    {
        return bytes.empty() ? 0 : (int) bytes[0];
    }

    // nibbleCount raw bytes, MSB-first, each byte's upper nibble always 0000.
    inline std::vector<uint8_t> encodeNibbles (int value, int nibbleCount)
    {
        std::vector<uint8_t> out ((size_t) nibbleCount, 0);
        for (int i = 0; i < nibbleCount; ++i)
        {
            int shift = 4 * (nibbleCount - i - 1);
            out[(size_t) i] = (uint8_t) ((value >> shift) & 0x0F);
        }
        return out;
    }

    inline int decodeNibbles (const std::vector<uint8_t>& bytes)
    {
        int value = 0;
        for (auto nibble : bytes)
            value = (value << 4) | (nibble & 0x0F);
        return value;
    }

    // Roland ASCII fields (e.g. tone_common's Name): one 7-bit char per byte,
    // space-padded to the field's declared size.
    inline std::vector<uint8_t> encodeAscii (const juce::String& text, int size)
    {
        std::vector<uint8_t> out ((size_t) size, (uint8_t) ' ');
        auto trimmed = text.substring (0, size);
        for (int i = 0; i < trimmed.length(); ++i)
        {
            auto c = (uint8_t) trimmed[i];
            out[(size_t) i] = c < 0x80 ? c : (uint8_t) ' ';
        }
        return out;
    }

    inline juce::String decodeAscii (const std::vector<uint8_t>& bytes)
    {
        juce::String result;
        for (auto b : bytes)
            result << (char) (b < 0x80 ? b : (uint8_t) ' ');
        return result;
    }
}
