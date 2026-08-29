#include "MainComponent.h"

namespace
{
    // name, CC number — the four documented-reliable CCs from brief §3.
    constexpr struct { const char* name; int cc; } kParams[] = {
        { "Cutoff",    74 },
        { "Resonance", 71 },
        { "Attack",    73 },
        { "Release",   72 },
    };
}

MainComponent::MainComponent()
{
    addAndMakeVisible (deviceLabel);
    addAndMakeVisible (deviceBox);
    addAndMakeVisible (refreshButton);
    addAndMakeVisible (channelLabel);
    addAndMakeVisible (channelBox);
    addAndMakeVisible (statusLabel);

    deviceBox.onChange = [this] { deviceSelected(); };
    refreshButton.onClick = [this] { refreshMidiDevices(); };

    for (int ch = 1; ch <= 16; ++ch)
        channelBox.addItem (juce::String (ch), ch);
    channelBox.setSelectedId (1, juce::dontSendNotification);
    channelBox.onChange = [this] { channelSelected(); };

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::orange);
    statusLabel.setText ("No MIDI output selected.", juce::dontSendNotification);

    for (size_t i = 0; i < params.size(); ++i)
    {
        auto& p = params[i];
        p.ccNumber = kParams[i].cc;

        p.slider.setRange (0.0, 127.0, 1.0);
        p.slider.setValue (64.0, juce::dontSendNotification);
        p.slider.setDoubleClickReturnValue (true, 64.0);
        addAndMakeVisible (p.slider);

        p.label.setText (juce::String (kParams[i].name) + " (CC" + juce::String (kParams[i].cc) + ")",
                          juce::dontSendNotification);
        p.label.setJustificationType (juce::Justification::centred);
        p.label.attachToComponent (&p.slider, false);
        addAndMakeVisible (p.label);
    }

    refreshMidiDevices();
    startTimerHz (50);

    setSize (640, 420);
}

MainComponent::~MainComponent()
{
    stopTimer();
}

void MainComponent::refreshMidiDevices()
{
    auto previouslySelected = deviceBox.getText();

    deviceBox.clear();
    auto names = transmitter.getAvailableDeviceNames();
    for (int i = 0; i < names.size(); ++i)
        deviceBox.addItem (names[i], i + 1);

    auto idx = names.indexOf (previouslySelected);
    if (idx >= 0)
        deviceBox.setSelectedId (idx + 1, juce::dontSendNotification);
    else if (names.isEmpty())
        statusLabel.setText ("No MIDI outputs found. Connect the MC-707 (Storage Mode must be OFF), then Refresh.",
                              juce::dontSendNotification);
    else
        deviceSelected();
}

void MainComponent::deviceSelected()
{
    auto name = deviceBox.getText();
    if (name.isEmpty())
        return;

    if (transmitter.openDevice (name))
    {
        statusLabel.setColour (juce::Label::textColourId, juce::Colours::lightgreen);
        statusLabel.setText ("Connected to \"" + name + "\" on channel " + juce::String (transmitter.getChannel())
                                  + ". Live CC only — does not read the patch's current values.",
                              juce::dontSendNotification);
    }
    else
    {
        statusLabel.setColour (juce::Label::textColourId, juce::Colours::orange);
        statusLabel.setText ("Could not open \"" + name + "\".", juce::dontSendNotification);
    }
}

void MainComponent::channelSelected()
{
    transmitter.setChannel (channelBox.getSelectedId());
    if (transmitter.isOpen())
        deviceSelected();
}

void MainComponent::timerCallback()
{
    for (auto& p : params)
    {
        auto value = (int) std::round (p.slider.getValue());
        if (value != p.lastSentValue)
        {
            transmitter.sendControlChange (p.ccNumber, value);
            p.lastSentValue = value;
        }
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto topRow = area.removeFromTop (28);
    deviceLabel.setBounds (topRow.removeFromLeft (90));
    refreshButton.setBounds (topRow.removeFromRight (80));
    channelBox.setBounds (topRow.removeFromRight (60));
    channelLabel.setBounds (topRow.removeFromRight (70));
    deviceBox.setBounds (topRow);

    area.removeFromTop (8);
    statusLabel.setBounds (area.removeFromTop (24));

    area.removeFromTop (24); // room for slider labels attached above each slider
    auto sliderArea = area.removeFromTop (juce::jmin (area.getHeight(), 260));

    auto sliderWidth = sliderArea.getWidth() / (int) params.size();
    for (auto& p : params)
        p.slider.setBounds (sliderArea.removeFromLeft (sliderWidth).reduced (12));
}
