#include "ToneEditComponent.h"
#include "../midi/SysExCodec.h"
#include "../model/ZenCoreAddressing.h"
#include <cmath>

namespace
{
    struct ControlSpec
    {
        const char* label;
        const char* blockId;
        const char* fieldName;
    };

    // AMP section, per tools/FINDINGS.md "M2 audible pass, round 1": the
    // on-device Easy Mode ADSR view maps Attack->Time1, Decay->Time3 (not
    // Time2), Sustain->Level3 (not Level0/1/2), Release->Time4.
    constexpr std::array<ControlSpec, 5> kControlSpecs { {
        { "ATTACK",  "partial_amp_env", "TVA Env Time1" },
        { "DECAY",   "partial_amp_env", "TVA Env Time3" },
        { "SUSTAIN", "partial_amp_env", "TVA Env Level3" },
        { "RELEASE", "partial_amp_env", "TVA Env Time4" },
        { "PAN",     "tone_partial",    "Pan" },
    } };
}

ToneEditComponent::ToneEditComponent (CCTransmitter& transmitterIn)
    : transmitter (transmitterIn)
{
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    addAndMakeVisible (titleLabel);

    subtitleLabel.setJustificationType (juce::Justification::centredLeft);
    subtitleLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    subtitleLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (subtitleLabel);

    for (int t = 1; t <= 8; ++t)
        trackBox.addItem ("Track " + juce::String (t), t);
    trackBox.setSelectedId (1, juce::dontSendNotification);
    trackBox.onChange = [this] { refreshAll(); };
    addAndMakeVisible (trackBox);
    addAndMakeVisible (trackLabel);

    for (int p = 1; p <= 4; ++p)
        partialBox.addItem ("Partial " + juce::String (p), p);
    partialBox.setSelectedId (1, juce::dontSendNotification);
    partialBox.onChange = [this] { refreshAll(); };
    addAndMakeVisible (partialBox);
    addAndMakeVisible (partialLabel);

    refreshButton.onClick = [this] { refreshAll(); };
    addAndMakeVisible (refreshButton);

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);
    setStatus ("Not read yet -- hit Refresh once connected.", juce::Colours::grey);

    for (size_t i = 0; i < controls.size(); ++i)
    {
        auto& c = controls[i];
        auto& spec = kControlSpecs[i];

        c.blockId = spec.blockId;
        c.descriptor = parameterMap.findField (c.blockId, spec.fieldName);

        c.label.setText (spec.label, juce::dontSendNotification);
        c.label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (c.label);

        if (c.descriptor)
            c.slider.setRange ((double) c.descriptor->rangeLow, (double) c.descriptor->rangeHigh, 1.0);
        else
            c.slider.setEnabled (false); // field not found in the map -- fail visibly, not silently
        addAndMakeVisible (c.slider);
    }

    confidenceNote.setText (
        "Only Track 1 / Partial 1 individually hardware-confirmed this session. Other tracks and "
        "partials use the same proven addressing formula but are unconfirmed per-slot.",
        juce::dontSendNotification);
    confidenceNote.setColour (juce::Label::textColourId, juce::Colours::grey);
    confidenceNote.setFont (juce::Font (juce::FontOptions (11.0f)));
    confidenceNote.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (confidenceNote);

    startTimerHz (50);
}

ToneEditComponent::~ToneEditComponent()
{
    stopTimer();
}

uint32_t ToneEditComponent::computeAddress (const juce::String& blockId, const FieldDescriptor& field) const
{
    auto trackIdx = trackBox.getSelectedId() - 1;
    auto partialIdx = partialBox.getSelectedId() - 1;

    // Track's own "track sound" slot (clipIdx = nullopt), not a specific clip tone.
    auto slotAddress = ZenCoreAddressing::getSlotAddress (
        parameterMap.trackBase(), parameterMap.toneAlignment(), trackIdx, std::nullopt);

    // Block-relative-offset and field-offset additions are safe with plain
    // addition (proven this session -- every block/field offset lands in a
    // byte that starts at 0 in every slot address and never nears 0x80).
    // Only the slot address itself needed the 7-bit-carry treatment above.
    auto blockBase = slotAddress + (uint32_t) parameterMap.blockRelativeOffset (blockId, partialIdx);
    return blockBase + (uint32_t) field.offsetWithinBlock;
}

void ToneEditComponent::setStatus (const juce::String& text, juce::Colour colour)
{
    statusLabel.setText (text, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, colour);
}

void ToneEditComponent::refreshAll()
{
    if (! parameterMap.isValid())
    {
        setStatus ("Parameter map failed to load.", juce::Colours::red);
        return;
    }

    if (! transmitter.isSysExReady())
    {
        setStatus ("Not connected -- select a device on the Control Panel tab.", juce::Colours::orange);
        return;
    }

    setStatus ("Reading...", juce::Colours::yellow);

    for (auto& c : controls)
    {
        c.initialized = false; // don't let the timer write stale values while re-reading
        if (! c.descriptor)
            continue;

        auto address = computeAddress (c.blockId, *c.descriptor);
        auto size = c.descriptor->size;
        auto encoding = c.descriptor->encoding;

        transmitter.sendSysExRead (address, size, [this, &c, encoding] (bool ok, std::vector<uint8_t> data)
        {
            if (! ok)
            {
                setStatus ("No reply -- check connection.", juce::Colours::orange);
                return;
            }

            auto value = (encoding == "uint7") ? SysExCodec::decodeUint7 (data) : SysExCodec::decodeNibbles (data);
            c.slider.setValue ((double) value, juce::dontSendNotification);
            c.lastSent = value;
            c.initialized = true;
            setStatus ("Read OK.", juce::Colours::lightgreen);
        });
    }
}

void ToneEditComponent::timerCallback()
{
    if (! transmitter.isSysExReady())
        return;

    for (auto& c : controls)
    {
        if (! c.descriptor || ! c.initialized)
            continue;

        auto value = (int) std::round (c.slider.getValue());
        if (value == c.lastSent)
            continue;

        c.lastSent = value;
        auto address = computeAddress (c.blockId, *c.descriptor);
        auto data = (c.descriptor->encoding == "uint7")
                        ? std::vector<uint8_t> { SysExCodec::encodeUint7 (value) }
                        : SysExCodec::encodeNibbles (value, c.descriptor->size);
        transmitter.sendSysExWrite (address, data);
    }
}

void ToneEditComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0d0d0d));
}

void ToneEditComponent::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto headerArea = area.removeFromTop (48);
    titleLabel.setBounds (headerArea.removeFromTop (26));
    subtitleLabel.setBounds (headerArea);

    area.removeFromTop (8);
    auto selectorArea = area.removeFromTop (28);
    trackLabel.setBounds (selectorArea.removeFromLeft (50));
    trackBox.setBounds (selectorArea.removeFromLeft (110));
    selectorArea.removeFromLeft (16);
    partialLabel.setBounds (selectorArea.removeFromLeft (60));
    partialBox.setBounds (selectorArea.removeFromLeft (110));
    selectorArea.removeFromLeft (16);
    refreshButton.setBounds (selectorArea.removeFromLeft (90));
    selectorArea.removeFromLeft (12);
    statusLabel.setBounds (selectorArea);

    area.removeFromTop (24);

    auto controlsArea = area.removeFromTop (280);
    auto controlWidth = controlsArea.getWidth() / (int) controls.size();
    for (auto& c : controls)
    {
        auto strip = controlsArea.removeFromLeft (controlWidth).reduced (18, 0);
        c.label.setBounds (strip.removeFromTop (20));
        strip.removeFromTop (6);
        c.slider.setBounds (strip);
    }

    area.removeFromTop (16);
    confidenceNote.setBounds (area.removeFromTop (32));
}
