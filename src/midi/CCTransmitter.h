#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

// Thin wrapper over a JUCE MidiOutput: opens a named device, sends Control
// Change / Program Change messages on an explicit channel. No SysEx here —
// see PLAN.md Phase 2+ for the SysEx layer, gated on the M1 viability check
// against real hardware (passed — see tools/FINDINGS.md).
//
// One shared instance is used by all 8 track strips; each strip supplies its
// own fixed MIDI channel (matching the real device's track-to-channel
// convention, confirmed empirically during M1) rather than this class storing
// a single channel the way Phase 1's version did.
class CCTransmitter
{
public:
    juce::StringArray getAvailableDeviceNames() const;

    // Returns true if the device was found and opened.
    bool openDevice (const juce::String& deviceName);
    void closeDevice();
    bool isOpen() const { return midiOutput != nullptr; }

    // channel: 1-16. ccNumber and value: 0-127. No-op if no device is open.
    void sendControlChange (int channel, int ccNumber, int value);

    // channel: 1-16. program: 0-127. No-op if no device is open.
    void sendProgramChange (int channel, int program);

private:
    std::unique_ptr<juce::MidiOutput> midiOutput;
};
