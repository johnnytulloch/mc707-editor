#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include <memory>
#include "../midi/CCTransmitter.h"
#include "TrackStripComponent.h"

// Control-surface view: 8 track strips (TrackStripComponent) plus a shared
// device picker, LCD-style readout, and Scene selector. Supersedes Phase 1's
// single 4-slider view. Hosted as one tab of MainComponent, alongside
// FileTransferComponent. See PLAN.md for the CC mapping table and the
// rationale behind each control's behaviour.
class ControlPanelComponent : public juce::Component,
                               private juce::Timer
{
public:
    ControlPanelComponent();
    ~ControlPanelComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refreshMidiDevices();
    void deviceSelected();
    void timerCallback() override;
    void updateLcd (const juce::String& paramLabel, int value);

    CCTransmitter transmitter;

    juce::Label titleLabel { {}, "GROOVEBOX MC-707" };
    juce::Label subtitleLabel { {}, "MIDI-EDITOR / REMOTE" };

    juce::Label deviceLabel { {}, "MIDI Output:" };
    juce::ComboBox deviceBox;
    juce::TextButton refreshButton { "Refresh" };
    juce::Label statusLabel;

    std::array<std::unique_ptr<TrackStripComponent>, 8> strips;

    juce::Label lcdLine1;
    juce::Label lcdLine2;

    juce::Label sceneLabel { {}, "SCENE" };
    juce::Slider sceneSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxBelow };

    // Scene changes go out as a Program Change on the device's "Control Channel" system setting
    // (UTILITY -> System Settings -> MIDI tab), confirmed working live -- see tools/FINDINGS.md.
    // That setting is per-device/per-project, not a fixed value, so it's user-selectable here
    // rather than hardcoded.
    juce::Label controlChannelLabel { {}, "Control Ch:" };
    juce::ComboBox controlChannelBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlPanelComponent)
};
