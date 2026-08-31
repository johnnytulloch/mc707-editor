#include "TrackStripComponent.h"

TrackStripComponent::TrackStripComponent (int trackIndex1to8, CCTransmitter& transmitterToUse, juce::Colour accentColour,
                                           LcdUpdateCallback onParamChangedCallback)
    : channel (trackIndex1to8), transmitter (transmitterToUse), accent (accentColour),
      onParamChanged (std::move (onParamChangedCallback))
{
    trackNumberLabel.setText (juce::String (trackIndex1to8), juce::dontSendNotification);
    trackNumberLabel.setJustificationType (juce::Justification::centred);
    trackNumberLabel.setColour (juce::Label::backgroundColourId, accent.withAlpha (0.25f));
    trackNumberLabel.setColour (juce::Label::textColourId, accent);
    addAndMakeVisible (trackNumberLabel);

    // Filter section: Cutoff (small, CC74), Filter macro (big, CC80), Reso (small, CC71).
    setUpKnob (filterKnobs[0], "CUTOFF", 74, false);
    setUpKnob (filterKnobs[1], "FILTER", 80, true);
    setUpKnob (filterKnobs[2], "RESO", 71, false);

    // Mod section: Att (small, CC73), Mod macro (big, CC81), Decay (small, CC75), Release (single, CC72).
    setUpKnob (modKnobs[0], "ATT", 73, false);
    setUpKnob (modKnobs[1], "MOD", 81, true);
    setUpKnob (modKnobs[2], "DECAY", 75, false);
    setUpKnob (modKnobs[3], "RELEASE", 72, false);

    // Portamento row: Depth (no documented CC), Time (CC5), Switch (CC65).
    setUpKnob (portaKnobs[0], "P.DEPTH", -1, false);
    setUpKnob (portaKnobs[1], "P.TIME", 5, false);
    setUpKnob (portaKnobs[2], "P.SWITCH", 65, false);

    // Vibrato row: Depth (CC77), Rate (CC76), Delay (CC78) -- brief §3 "76/77/78 = rate/depth/delay".
    setUpKnob (vibKnobs[0], "V.DEPTH", 77, false);
    setUpKnob (vibKnobs[1], "V.RATE", 76, false);
    setUpKnob (vibKnobs[2], "V.DELAY", 78, false);

    // FX section: Delay Send (no documented CC), FX macro (big, CC82), Chorus Send (small, CC92), Rev Send (single, CC91).
    setUpKnob (fxKnobs[0], "DLY SEND", -1, false);
    setUpKnob (fxKnobs[1], "FX", 82, true);
    setUpKnob (fxKnobs[2], "CHO SEND", 92, false);
    setUpKnob (fxKnobs[3], "REV SEND", 91, false);

    setUpKnob (panKnob, "PAN", 10, false);

    for (auto* groupLabel : { &filterGroupLabel, &modGroupLabel, &fxGroupLabel })
    {
        groupLabel->setJustificationType (juce::Justification::centred);
        groupLabel->setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        groupLabel->setColour (juce::Label::textColourId, accent);
        addAndMakeVisible (*groupLabel);
    }

    fader.setRange (0.0, 127.0, 1.0);
    fader.setValue (100.0, juce::dontSendNotification);
    fader.setColour (juce::Slider::thumbColourId, accent);
    fader.setColour (juce::Slider::trackColourId, accent.withAlpha (0.5f));
    addAndMakeVisible (fader);

    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::red);
    muteButton.onClick = [this]
    {
        muted = muteButton.getToggleState();
        if (muted)
        {
            transmitter.sendControlChange (channel, 7, 0);
            lastSentFaderValue = 0;
            if (onParamChanged)
                onParamChanged ("T" + juce::String (channel) + "-MUTE", 0);
        }
        else
        {
            auto v = (int) std::round (fader.getValue());
            transmitter.sendControlChange (channel, 7, v);
            lastSentFaderValue = v;
            if (onParamChanged)
                onParamChanged ("T" + juce::String (channel) + "-LEVEL", v);
        }
    };
    addAndMakeVisible (muteButton);

    clipSlider.setRange (1.0, 16.0, 1.0);
    clipSlider.setValue (1.0, juce::dontSendNotification);
    clipSlider.onValueChange = [this]
    {
        auto clipNumber = (int) std::round (clipSlider.getValue());
        transmitter.sendProgramChange (channel, clipNumber - 1);
        if (onParamChanged)
            onParamChanged ("T" + juce::String (channel) + "-CLIP", clipNumber);
    };
    addAndMakeVisible (clipSlider);
    addAndMakeVisible (clipLabel);
    clipLabel.setJustificationType (juce::Justification::centred);
    clipLabel.setFont (juce::Font (juce::FontOptions (11.0f)));

    panLabelBelow.setJustificationType (juce::Justification::centred);
    panLabelBelow.setFont (juce::Font (juce::FontOptions (11.0f)));
    addAndMakeVisible (panLabelBelow);
}

void TrackStripComponent::setUpKnob (Knob& k, const juce::String& text, int cc, bool big)
{
    k.ccNumber = cc;
    k.big = big;

    k.slider.setRange (0.0, 127.0, 1.0);
    k.slider.setValue (64.0, juce::dontSendNotification);
    k.slider.setDoubleClickReturnValue (true, 64.0);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    k.slider.setEnabled (cc >= 0);
    addAndMakeVisible (k.slider);

    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::Font (juce::FontOptions (big ? 12.0f : 10.0f)));
    if (cc < 0)
        k.label.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (k.label);
}

void TrackStripComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));
    g.setColour (accent.withAlpha (0.4f));
    g.drawRect (getLocalBounds(), 1);
}

void TrackStripComponent::resized()
{
    auto area = getLocalBounds().reduced (4);

    trackNumberLabel.setBounds (area.removeFromTop (22));
    area.removeFromTop (4);

    // FILTER
    filterGroupLabel.setBounds (area.removeFromTop (14));
    {
        auto row = area.removeFromTop (58);
        auto third = row.getWidth() / 3;
        filterKnobs[0].slider.setBounds (row.removeFromLeft (third).reduced (6, 10));
        filterKnobs[1].slider.setBounds (row.removeFromLeft (third).reduced (2));
        filterKnobs[2].slider.setBounds (row.reduced (6, 10));

        auto labelRow = area.removeFromTop (14);
        auto labelThird = labelRow.getWidth() / 3;
        filterKnobs[0].label.setBounds (labelRow.removeFromLeft (labelThird));
        labelRow.removeFromLeft (labelThird);
        filterKnobs[2].label.setBounds (labelRow);
    }
    area.removeFromTop (6);

    // MOD (+ Release below)
    modGroupLabel.setBounds (area.removeFromTop (14));
    {
        auto row = area.removeFromTop (58);
        auto third = row.getWidth() / 3;
        modKnobs[0].slider.setBounds (row.removeFromLeft (third).reduced (6, 10));
        modKnobs[1].slider.setBounds (row.removeFromLeft (third).reduced (2));
        modKnobs[2].slider.setBounds (row.reduced (6, 10));

        auto labelRow = area.removeFromTop (14);
        auto labelThird = labelRow.getWidth() / 3;
        modKnobs[0].label.setBounds (labelRow.removeFromLeft (labelThird));
        labelRow.removeFromLeft (labelThird);
        modKnobs[2].label.setBounds (labelRow);
    }
    area.removeFromTop (2);
    {
        auto releaseArea = area.removeFromTop (44);
        modKnobs[3].slider.setBounds (releaseArea.withSizeKeepingCentre (36, 36));
        modKnobs[3].label.setBounds (area.removeFromTop (14));
    }
    area.removeFromTop (6);

    // Porta row (3 equal small knobs)
    {
        auto row = area.removeFromTop (40);
        auto third = row.getWidth() / 3;
        for (auto* k : { &portaKnobs[0], &portaKnobs[1], &portaKnobs[2] })
            k->slider.setBounds (row.removeFromLeft (third).reduced (4));
        auto labelRow = area.removeFromTop (12);
        auto labelThird = labelRow.getWidth() / 3;
        for (auto* k : { &portaKnobs[0], &portaKnobs[1], &portaKnobs[2] })
            k->label.setBounds (labelRow.removeFromLeft (labelThird));
    }
    area.removeFromTop (4);

    // Vibrato row (3 equal small knobs)
    {
        auto row = area.removeFromTop (40);
        auto third = row.getWidth() / 3;
        for (auto* k : { &vibKnobs[0], &vibKnobs[1], &vibKnobs[2] })
            k->slider.setBounds (row.removeFromLeft (third).reduced (4));
        auto labelRow = area.removeFromTop (12);
        auto labelThird = labelRow.getWidth() / 3;
        for (auto* k : { &vibKnobs[0], &vibKnobs[1], &vibKnobs[2] })
            k->label.setBounds (labelRow.removeFromLeft (labelThird));
    }
    area.removeFromTop (6);

    // FX (+ Rev Send below)
    fxGroupLabel.setBounds (area.removeFromTop (14));
    {
        auto row = area.removeFromTop (58);
        auto third = row.getWidth() / 3;
        fxKnobs[0].slider.setBounds (row.removeFromLeft (third).reduced (6, 10));
        fxKnobs[1].slider.setBounds (row.removeFromLeft (third).reduced (2));
        fxKnobs[2].slider.setBounds (row.reduced (6, 10));

        auto labelRow = area.removeFromTop (14);
        auto labelThird = labelRow.getWidth() / 3;
        fxKnobs[0].label.setBounds (labelRow.removeFromLeft (labelThird));
        labelRow.removeFromLeft (labelThird);
        fxKnobs[2].label.setBounds (labelRow);
    }
    area.removeFromTop (2);
    {
        auto revArea = area.removeFromTop (44);
        fxKnobs[3].slider.setBounds (revArea.withSizeKeepingCentre (36, 36));
        fxKnobs[3].label.setBounds (area.removeFromTop (14));
    }
    area.removeFromTop (8);

    muteButton.setBounds (area.removeFromTop (26).reduced (12, 0));
    area.removeFromTop (8);

    // Fader + Clip number + Pan, side by side at the bottom.
    auto bottomArea = area;
    auto faderColumn = bottomArea.removeFromLeft (bottomArea.getWidth() * 2 / 5);
    fader.setBounds (faderColumn.reduced (10, 0));

    auto clipColumn = bottomArea;
    auto clipControlArea = clipColumn.removeFromTop (54);
    clipSlider.setBounds (clipControlArea.reduced (2));
    clipLabel.setBounds (clipColumn.removeFromTop (14));
    clipColumn.removeFromTop (6);
    panKnob.slider.setBounds (clipColumn.removeFromTop (44).withSizeKeepingCentre (40, 40));
    panLabelBelow.setBounds (clipColumn.removeFromTop (14));
}

void TrackStripComponent::pollGroup (Knob* knobs, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        auto& k = knobs[i];
        if (k.ccNumber < 0)
            continue;

        auto value = (int) std::round (k.slider.getValue());
        if (value != k.lastSent)
        {
            transmitter.sendControlChange (channel, k.ccNumber, value);
            k.lastSent = value;
            if (onParamChanged)
                onParamChanged ("T" + juce::String (channel) + "-" + k.label.getText(), value);
        }
    }
}

void TrackStripComponent::pollAndSend()
{
    pollGroup (filterKnobs.data(), filterKnobs.size());
    pollGroup (modKnobs.data(), modKnobs.size());
    pollGroup (portaKnobs.data(), portaKnobs.size());
    pollGroup (vibKnobs.data(), vibKnobs.size());
    pollGroup (fxKnobs.data(), fxKnobs.size());
    pollGroup (&panKnob, 1);

    if (! muted)
    {
        auto value = (int) std::round (fader.getValue());
        if (value != lastSentFaderValue)
        {
            transmitter.sendControlChange (channel, 7, value);
            lastSentFaderValue = value;
            if (onParamChanged)
                onParamChanged ("T" + juce::String (channel) + "-LEVEL", value);
        }
    }
}
