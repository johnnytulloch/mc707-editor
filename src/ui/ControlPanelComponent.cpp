#include "ControlPanelComponent.h"

namespace
{
    const juce::Colour kTrackColours[8] = {
        juce::Colours::red, juce::Colours::limegreen, juce::Colours::mediumspringgreen, juce::Colours::cyan,
        juce::Colours::lightskyblue, juce::Colours::royalblue, juce::Colours::mediumpurple, juce::Colours::magenta,
    };
}

ControlPanelComponent::ControlPanelComponent()
{
    titleLabel.setJustificationType (juce::Justification::centredRight);
    titleLabel.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    addAndMakeVisible (titleLabel);

    subtitleLabel.setJustificationType (juce::Justification::centredRight);
    subtitleLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    subtitleLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (subtitleLabel);

    addAndMakeVisible (deviceLabel);
    addAndMakeVisible (deviceBox);
    addAndMakeVisible (refreshButton);
    deviceBox.onChange = [this] { deviceSelected(); };
    refreshButton.onClick = [this] { refreshMidiDevices(); };

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::orange);
    statusLabel.setText ("No MIDI output selected.", juce::dontSendNotification);
    addAndMakeVisible (statusLabel);

    for (int i = 0; i < 8; ++i)
    {
        strips[(size_t) i] = std::make_unique<TrackStripComponent> (
            i + 1, transmitter, kTrackColours[i],
            [this] (const juce::String& paramLabel, int value) { updateLcd (paramLabel, value); });
        addAndMakeVisible (*strips[(size_t) i]);
    }

    lcdLine1.setJustificationType (juce::Justification::centred);
    lcdLine1.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    lcdLine1.setColour (juce::Label::backgroundColourId, juce::Colour (0xff9fc9a0));
    lcdLine1.setColour (juce::Label::textColourId, juce::Colours::black);
    lcdLine1.setText ("-", juce::dontSendNotification);
    addAndMakeVisible (lcdLine1);

    lcdLine2.setJustificationType (juce::Justification::centred);
    lcdLine2.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    lcdLine2.setColour (juce::Label::backgroundColourId, juce::Colour (0xff9fc9a0));
    lcdLine2.setColour (juce::Label::textColourId, juce::Colours::black);
    lcdLine2.setText ("-", juce::dontSendNotification);
    addAndMakeVisible (lcdLine2);

    sceneLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (sceneLabel);

    for (int ch = 1; ch <= 16; ++ch)
        controlChannelBox.addItem (juce::String (ch), ch);
    controlChannelBox.setSelectedId (12, juce::dontSendNotification); // confirmed working value, still user-changeable
    addAndMakeVisible (controlChannelBox);
    addAndMakeVisible (controlChannelLabel);

    sceneSlider.setRange (1.0, 8.0, 1.0);
    sceneSlider.setValue (1.0, juce::dontSendNotification);
    // Scene changes are sent as Program Change (scene - 1) on the device's Control Channel
    // system setting -- confirmed working live against real hardware, see tools/FINDINGS.md.
    // Requires the device's "Control Rx" system setting to be ON.
    sceneSlider.onValueChange = [this]
    {
        auto scene = (int) std::round (sceneSlider.getValue());
        auto controlChannel = controlChannelBox.getSelectedId();
        transmitter.sendProgramChange (controlChannel, scene - 1);
        updateLcd ("SCENE (ch" + juce::String (controlChannel) + ")", scene);
    };
    addAndMakeVisible (sceneSlider);

    refreshMidiDevices();
    startTimerHz (50);
}

ControlPanelComponent::~ControlPanelComponent()
{
    stopTimer();
}

void ControlPanelComponent::refreshMidiDevices()
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
        statusLabel.setText ("No MIDI outputs found. Connect the MC-707, then Refresh.", juce::dontSendNotification);
    else
        deviceSelected();
}

void ControlPanelComponent::deviceSelected()
{
    auto name = deviceBox.getText();
    if (name.isEmpty())
        return;

    if (transmitter.openDevice (name))
    {
        statusLabel.setColour (juce::Label::textColourId, juce::Colours::lightgreen);
        statusLabel.setText ("Connected to \"" + name + "\". Tracks 1-8 send on MIDI channels 1-8.",
                              juce::dontSendNotification);
    }
    else
    {
        statusLabel.setColour (juce::Label::textColourId, juce::Colours::orange);
        statusLabel.setText ("Could not open \"" + name + "\".", juce::dontSendNotification);
    }
}

void ControlPanelComponent::updateLcd (const juce::String& paramLabel, int value)
{
    lcdLine1.setText (paramLabel, juce::dontSendNotification);
    lcdLine2.setText (juce::String (value), juce::dontSendNotification);
}

void ControlPanelComponent::timerCallback()
{
    for (auto& strip : strips)
        strip->pollAndSend();
}

void ControlPanelComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0d0d0d));
}

void ControlPanelComponent::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto headerArea = area.removeFromTop (56);
    auto rightHeader = headerArea.removeFromRight (420);
    titleLabel.setBounds (rightHeader.removeFromTop (28));
    subtitleLabel.setBounds (rightHeader);

    auto deviceArea = headerArea;
    refreshButton.setBounds (deviceArea.removeFromRight (80));
    deviceArea.removeFromRight (8);
    deviceLabel.setBounds (deviceArea.removeFromLeft (90));
    deviceBox.setBounds (deviceArea.removeFromLeft (280));

    area.removeFromTop (4);
    statusLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (8);

    auto sidePanel = area.removeFromRight (200);
    area.removeFromRight (12);

    auto lcdArea = sidePanel.removeFromTop (70);
    lcdLine1.setBounds (lcdArea.removeFromTop (34).reduced (2));
    lcdLine2.setBounds (lcdArea.reduced (2));

    sidePanel.removeFromTop (16);
    auto sceneArea = sidePanel.removeFromTop (70);
    sceneLabel.setBounds (sceneArea.removeFromTop (18));
    sceneSlider.setBounds (sceneArea.reduced (20, 0));

    sidePanel.removeFromTop (10);
    auto controlChannelArea = sidePanel.removeFromTop (24);
    controlChannelLabel.setBounds (controlChannelArea.removeFromLeft (80));
    controlChannelBox.setBounds (controlChannelArea);

    auto stripWidth = area.getWidth() / 8;
    for (auto& strip : strips)
        strip->setBounds (area.removeFromLeft (stripWidth));
}
