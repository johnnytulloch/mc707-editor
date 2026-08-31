#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <deque>
#include <functional>
#include <vector>

// Thin wrapper over a JUCE MidiOutput/MidiInput pair: sends Control Change /
// Program Change on an explicit channel (Phase 1), and RQ1/DT1 SysEx
// read/write (M3 pilot) -- see tools/FINDINGS.md for the message formats,
// checksum, and the documented reply-address bug this class works around.
// Message building lives in SysExCodec.h; this class only owns the MIDI I/O.
//
// One shared instance lives in MainComponent and is passed by reference to
// every tab that needs it (ControlPanelComponent, ToneEditComponent) --
// MIDI output ports are exclusive on Windows (confirmed empirically this
// session), so only one connection to the device can exist at a time.
class CCTransmitter : private juce::MidiInputCallback,
                       private juce::Timer
{
public:
    CCTransmitter();
    ~CCTransmitter() override;

    juce::StringArray getAvailableDeviceNames() const;

    // Opens the named MIDI output, and -- best-effort -- a matching MIDI
    // input (longest-common-prefix name match against available inputs,
    // e.g. output "MC-707 1" matches input "MC-707 0", proven against this
    // exact hardware in tools/mc707.py::find_ports()). Returns true if the
    // output opened; CC/PC keep working even if no matching input was found,
    // but SysEx read/write then won't (see isSysExReady()).
    bool openDevice (const juce::String& deviceName);
    void closeDevice();
    bool isOpen() const { return midiOutput != nullptr; }
    bool isSysExReady() const { return midiOutput != nullptr && midiInput != nullptr; }

    // channel: 1-16. ccNumber and value: 0-127. No-op if no device is open.
    void sendControlChange (int channel, int ccNumber, int value);

    // channel: 1-16. program: 0-127. No-op if no device is open.
    void sendProgramChange (int channel, int program);

    // Fire-and-forget DT1 write. No-op if no device is open. No per-write
    // read-back in this pilot -- see docs/M2_PLAN.md / the Tone Edit plan
    // for why (initial read on load covers the "never write blind" rule).
    void sendSysExWrite (uint32_t address, std::vector<uint8_t> data);

    // Queues an RQ1 read. Reads are serviced one at a time: the device's
    // DT1 reply carries the correct payload but a wrong address field (the
    // documented "reply-address bug", tools/FINDINGS.md), so concurrent
    // requests can't be told apart by address. callback runs on the message
    // thread with (true, data) on success, or (false, {}) on timeout / no
    // SysEx-capable connection.
    using ReadCallback = std::function<void (bool success, std::vector<uint8_t> data)>;
    void sendSysExRead (uint32_t address, int size, ReadCallback callback);

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void timerCallback() override;
    void serviceQueue();
    void completeCurrentRead (bool success, std::vector<uint8_t> data);

    struct PendingRead
    {
        uint32_t address;
        int size;
        ReadCallback callback;
    };

    std::unique_ptr<juce::MidiOutput> midiOutput;
    std::unique_ptr<juce::MidiInput> midiInput;

    std::deque<PendingRead> readQueue;
    bool waitingForReply = false;
    juce::int64 requestSentMs = 0;
    static constexpr int kReadTimeoutMs = 1000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CCTransmitter)
};
