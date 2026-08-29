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

void CCTransmitter::setChannel (int midiChannel1to16)
{
    channel = juce::jlimit (1, 16, midiChannel1to16);
}

void CCTransmitter::sendControlChange (int ccNumber, int value)
{
    if (midiOutput == nullptr)
        return;

    midiOutput->sendMessageNow (
        juce::MidiMessage::controllerEvent (channel, ccNumber, juce::jlimit (0, 127, value)));
}
