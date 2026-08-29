#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include "../midi/CCTransmitter.h"

// Phase 1 "CC Quick-Edit": four sliders wired to the MC-707's documented
// reliable CC numbers (brief §3). No SysEx, no patch read-back — this is a
// live performance surface, not the eventual stored-patch editor (Phase 5+).
class MainComponent : public juce::Component,
                       private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refreshMidiDevices();
    void deviceSelected();
    void channelSelected();
    void timerCallback() override;

    struct ParamSlider
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        int ccNumber = 0;
        int lastSentValue = -1;
    };

    CCTransmitter transmitter;

    juce::Label deviceLabel { {}, "MIDI Output:" };
    juce::ComboBox deviceBox;
    juce::TextButton refreshButton { "Refresh" };

    juce::Label channelLabel { {}, "Channel:" };
    juce::ComboBox channelBox;

    juce::Label statusLabel;

    std::array<ParamSlider, 4> params;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
