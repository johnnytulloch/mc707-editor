#include "CCTransmitter.h"

juce::StringArray CCTransmitter::getAvailableDeviceNames() const
{
    juce::StringArray names;
    for (auto& device : juce::MidiOutput::getAvailableDevices())
        names.add (device.name);
    return names;
}

bool CCTransmitter::openDevice (const juce::String& deviceName)
{
    closeDevice();

    for (auto& device : juce::MidiOutput::getAvailableDevices())
    {
        if (device.name == deviceName)
        {
            midiOutput = juce::MidiOutput::openDevice (device.identifier);
            return midiOutput != nullptr;
        }
    }
    return false;
}

void CCTransmitter::closeDevice()
{
    midiOutput.reset();
}

void CCTransmitter::sendControlChange (int channel, int ccNumber, int value)
{
    if (midiOutput == nullptr)
        return;

    midiOutput->sendMessageNow (
        juce::MidiMessage::controllerEvent (juce::jlimit (1, 16, channel), ccNumber, juce::jlimit (0, 127, value)));
}

void CCTransmitter::sendProgramChange (int channel, int program)
{
    if (midiOutput == nullptr)
        return;

    midiOutput->sendMessageNow (
        juce::MidiMessage::programChange (juce::jlimit (1, 16, channel), juce::jlimit (0, 127, program)));
}
