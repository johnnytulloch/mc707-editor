#include "ToneSnapshot.h"
#include "../midi/CCTransmitter.h"
#include "../midi/SysExCodec.h"
#include <juce_events/juce_events.h>
#include <memory>

std::vector<ChunkPlanEntry> computeChunkPlan (const ParameterMap& map)
{
    std::vector<ChunkPlanEntry> plan;

    for (auto blockId : map.blockIds())
    {
        auto fields = map.allFields (blockId);
        if (fields.empty())
            continue;

        int low = fields.front().offsetWithinBlock;
        int high = fields.front().offsetWithinBlock + fields.front().size;
        for (auto& f : fields)
        {
            low = juce::jmin (low, f.offsetWithinBlock);
            high = juce::jmax (high, f.offsetWithinBlock + f.size);
        }

        auto perPartial = map.blockIsPerPartial (blockId);
        auto partialCount = perPartial ? 4 : 1;
        for (int p = 0; p < partialCount; ++p)
        {
            ChunkPlanEntry entry;
            entry.blockId = blockId;
            entry.partialIdx = perPartial ? p : -1;
            entry.spanOffset = low;
            entry.spanSize = high - low;
            plan.push_back (entry);
        }
    }

    return plan;
}

namespace
{
    uint32_t chunkAddress (const ParameterMap& map, uint32_t slotBase, const juce::String& blockId, int partialIdx, int spanOffset)
    {
        // Same plain-addition pattern as ToneEditComponent::computeAddress --
        // block-relative-offset and within-block offsets are proven safe
        // added directly on top of a slot address (only the slot address
        // itself needs the 7-bit-carry treatment, done inside getSlotAddress).
        auto blockBase = slotBase + (uint32_t) map.blockRelativeOffset (blockId, juce::jmax (0, partialIdx));
        return blockBase + (uint32_t) spanOffset;
    }

    // Sends one captured chunk's DT1 write per timer tick instead of firing
    // all of them in a single tight loop. Found live (2026-09-03): a bulk
    // write of ~30 chunks back-to-back with no pacing produced a tone with a
    // correct Name/Category (an early chunk) but no sound at all (later
    // chunks -- the oscillator/envelope/partial-switch data -- silently
    // dropped). Matches a documented MC-707 SysEx quirk (the B67 ZEN
    // EditorLib manual explicitly warns its own bulk transfers need pacing
    // for "anomalies... in the SysEx implementation of MC-101 and MC-707").
    // sendSysExWrite has no reply to wait for (DT1 writes aren't ack'd), so
    // this paces on a fixed interval rather than the read side's
    // wait-for-reply queue. 40ms is a first, untuned guess -- increase if
    // writes are still unreliable, see tools/FINDINGS.md.
    class PacedSnapshotWriter : private juce::Timer
    {
    public:
        PacedSnapshotWriter (CCTransmitter& transmitterIn, const ParameterMap& mapIn, uint32_t slotBaseIn,
                              std::vector<ToneSnapshot::BlockChunk> chunksIn, SnapshotWriteCallback callbackIn)
            : transmitter (transmitterIn), map (mapIn), slotBase (slotBaseIn),
              chunks (std::move (chunksIn)), callback (std::move (callbackIn))
        {
            startTimer (kWriteIntervalMs);
        }

    private:
        void timerCallback() override
        {
            if (index >= chunks.size())
            {
                stopTimer();
                callback (true);
                delete this; // self-deleting one-shot async helper, matches this codebase's FileChooser-style async patterns
                return;
            }

            auto& chunk = chunks[index];
            auto address = chunkAddress (map, slotBase, chunk.blockId, chunk.partialIdx, chunk.spanOffset);
            transmitter.sendSysExWrite (address, chunk.bytes);
            ++index;
        }

        static constexpr int kWriteIntervalMs = 40;
        CCTransmitter& transmitter;
        const ParameterMap& map; // owned by the caller (ToneEditComponent), outlives this short-lived async write
        uint32_t slotBase;
        std::vector<ToneSnapshot::BlockChunk> chunks;
        size_t index = 0;
        SnapshotWriteCallback callback;
    };

    void decodeNameAndCategory (const ParameterMap& map, ToneSnapshot& snapshot)
    {
        for (auto& c : snapshot.chunks)
        {
            if (c.blockId != "tone_common")
                continue;

            if (auto nameField = map.findField ("tone_common", "Name"))
            {
                auto start = nameField->offsetWithinBlock - c.spanOffset;
                if (start >= 0 && start + nameField->size <= (int) c.bytes.size())
                    snapshot.name = SysExCodec::decodeAscii ({ c.bytes.begin() + start, c.bytes.begin() + start + nameField->size }).trim();
            }
            if (auto categoryField = map.findField ("tone_common", "Category"))
            {
                auto start = categoryField->offsetWithinBlock - c.spanOffset;
                if (start >= 0 && start + categoryField->size <= (int) c.bytes.size())
                    snapshot.category = SysExCodec::decodeUint7 ({ c.bytes.begin() + start, c.bytes.begin() + start + categoryField->size });
            }
            return;
        }
    }
}

void readToneSnapshot (CCTransmitter& transmitter, const ParameterMap& map,
                        uint32_t slotBase, SnapshotReadCallback callback)
{
    auto plan = computeChunkPlan (map);
    if (plan.empty())
    {
        callback (false, {});
        return;
    }

    auto snapshot = std::make_shared<ToneSnapshot>();
    snapshot->chunks.resize (plan.size());
    auto remaining = std::make_shared<int> ((int) plan.size());
    auto failed = std::make_shared<bool> (false);
    auto mapPtr = &map; // ParameterMap outlives this call (owned by ToneEditComponent)

    for (size_t i = 0; i < plan.size(); ++i)
    {
        const auto& entry = plan[i];
        auto address = chunkAddress (map, slotBase, entry.blockId, entry.partialIdx, entry.spanOffset);

        transmitter.sendSysExRead (address, entry.spanSize,
            [snapshot, remaining, failed, i, entry, callback, mapPtr] (bool ok, std::vector<uint8_t> data)
            {
                if (! ok || (int) data.size() != entry.spanSize)
                {
                    *failed = true;
                }
                else
                {
                    ToneSnapshot::BlockChunk chunk;
                    chunk.blockId = entry.blockId;
                    chunk.partialIdx = entry.partialIdx;
                    chunk.spanOffset = entry.spanOffset;
                    chunk.bytes = std::move (data);
                    snapshot->chunks[i] = std::move (chunk);
                }

                if (--(*remaining) == 0)
                {
                    if (*failed)
                    {
                        callback (false, {});
                        return;
                    }

                    decodeNameAndCategory (*mapPtr, *snapshot);
                    callback (true, *snapshot);
                }
            });
    }
}

void writeToneSnapshot (CCTransmitter& transmitter, const ParameterMap& map,
                         uint32_t slotBase, const ToneSnapshot& snapshot, SnapshotWriteCallback callback)
{
    if (snapshot.chunks.empty())
    {
        callback (false);
        return;
    }

    // Paced, not a tight loop -- see PacedSnapshotWriter's comment above for
    // why. Self-deletes once every chunk has been sent.
    new PacedSnapshotWriter (transmitter, map, slotBase, snapshot.chunks, std::move (callback));
}

juce::var toneSnapshotToVar (const ToneSnapshot& snapshot)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("name", snapshot.name);
    obj->setProperty ("category", snapshot.category);

    juce::Array<juce::var> chunksArr;
    for (auto& c : snapshot.chunks)
    {
        auto* co = new juce::DynamicObject();
        co->setProperty ("blockId", c.blockId);
        co->setProperty ("partialIdx", c.partialIdx);
        co->setProperty ("spanOffset", c.spanOffset);
        co->setProperty ("bytesHex", juce::String::toHexString (c.bytes.data(), (int) c.bytes.size()));
        chunksArr.add (juce::var (co));
    }
    obj->setProperty ("chunks", chunksArr);

    return juce::var (obj);
}

std::optional<ToneSnapshot> toneSnapshotFromVar (const juce::var& v)
{
    if (! v.isObject())
        return std::nullopt;

    ToneSnapshot snapshot;
    snapshot.name = v.getProperty ("name", {}).toString();
    snapshot.category = (int) v.getProperty ("category", 0);

    auto chunksVar = v.getProperty ("chunks", {});
    auto* arr = chunksVar.getArray();
    if (arr == nullptr)
        return std::nullopt;

    for (auto& cv : *arr)
    {
        ToneSnapshot::BlockChunk chunk;
        chunk.blockId = cv.getProperty ("blockId", {}).toString();
        chunk.partialIdx = (int) cv.getProperty ("partialIdx", -1);
        chunk.spanOffset = (int) cv.getProperty ("spanOffset", 0);

        juce::MemoryBlock mb;
        mb.loadFromHexString (cv.getProperty ("bytesHex", {}).toString());
        chunk.bytes.assign (static_cast<const uint8_t*> (mb.getData()),
                             static_cast<const uint8_t*> (mb.getData()) + mb.getSize());

        snapshot.chunks.push_back (std::move (chunk));
    }

    return snapshot;
}
