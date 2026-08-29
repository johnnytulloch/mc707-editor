#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

// Thin wrapper over a JUCE MidiOutput: opens a named device, sends Control
// Change messages on a chosen channel. No SysEx here — see PLAN.md Phase 2+
// for the SysEx layer, gated on the M1 viability check against real hardware.
class CCTransmitter
{
public:
    juce::StringArray getAvailableDeviceNames() const;

    // Returns true if the device was found and opened.
    bool openDevice (const juce::String& deviceName);
    void closeDevice();
    bool isOpen() const { return midiOutput != nullptr; }

    void setChannel (int midiChannel1to16);
    int getChannel() const { return channel; }

    // ccNumber and value are both 0-127. No-op if no device is open.
    void sendControlChange (int ccNumber, int value);

private:
    std::unique_ptr<juce::MidiOutput> midiOutput;
    int channel = 1;
};
