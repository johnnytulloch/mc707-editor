#include "CCTransmitter.h"
#include "SysExCodec.h"

namespace
{
    int commonPrefixLength (const juce::String& a, const juce::String& b)
    {
        auto n = juce::jmin (a.length(), b.length());
        int i = 0;
        while (i < n && a[i] == b[i])
            ++i;
        return i;
    }
}

CCTransmitter::CCTransmitter() = default;

CCTransmitter::~CCTransmitter()
{
    stopTimer();
    closeDevice();
}

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
            break;
        }
    }
    if (midiOutput == nullptr)
        return false;

    // Best-effort matching input for SysEx replies (e.g. output "MC-707 1"
    // <-> input "MC-707 0"). CC/PC still work below even if this fails.
    juce::MidiDeviceInfo bestInput;
    int bestScore = 0;
    for (auto& device : juce::MidiInput::getAvailableDevices())
    {
        auto score = commonPrefixLength (device.name, deviceName);
        if (score > bestScore)
        {
            bestScore = score;
            bestInput = device;
        }
    }
    if (bestScore > 0)
    {
        midiInput = juce::MidiInput::openDevice (bestInput.identifier, this);
        if (midiInput != nullptr)
            midiInput->start();
    }

    startTimerHz (10); // read-timeout polling
    return true;
}

void CCTransmitter::closeDevice()
{
    readQueue.clear();
    waitingForReply = false;

    if (midiInput != nullptr)
        midiInput->stop();
    midiInput.reset();
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

void CCTransmitter::sendSysExWrite (uint32_t address, std::vector<uint8_t> data)
{
    if (midiOutput == nullptr)
        return;

    auto body = SysExCodec::buildDt1 (address, data);
    midiOutput->sendMessageNow (juce::MidiMessage::createSysExMessage (body.data(), (int) body.size()));
}

void CCTransmitter::sendSysExRead (uint32_t address, int size, ReadCallback callback)
{
    readQueue.push_back ({ address, size, std::move (callback) });
    serviceQueue();
}

void CCTransmitter::serviceQueue()
{
    if (waitingForReply || readQueue.empty())
        return;

    if (midiOutput == nullptr || midiInput == nullptr)
    {
        completeCurrentRead (false, {});
        return;
    }

    auto& pending = readQueue.front();
    auto body = SysExCodec::buildRq1 (pending.address, (uint32_t) pending.size);
    midiOutput->sendMessageNow (juce::MidiMessage::createSysExMessage (body.data(), (int) body.size()));
    waitingForReply = true;
    requestSentMs = juce::Time::currentTimeMillis();
}

void CCTransmitter::completeCurrentRead (bool success, std::vector<uint8_t> data)
{
    if (readQueue.empty())
        return;

    auto callback = std::move (readQueue.front().callback);
    readQueue.pop_front();
    waitingForReply = false;

    if (callback)
        callback (success, std::move (data));

    serviceQueue();
}

void CCTransmitter::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    if (! message.isSysEx() || ! waitingForReply)
        return;

    auto parsed = SysExCodec::parseDt1 (message.getSysExData(), message.getSysExDataSize());

    juce::MessageManager::callAsync ([this, parsed]
    {
        completeCurrentRead (parsed.valid, parsed.data);
    });
}

void CCTransmitter::timerCallback()
{
    if (waitingForReply && juce::Time::currentTimeMillis() - requestSentMs > kReadTimeoutMs)
        completeCurrentRead (false, {});
}
