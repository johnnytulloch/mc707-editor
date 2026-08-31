#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include <functional>
#include "../midi/CCTransmitter.h"

// One mixer-style channel strip (Filter/Mod/Porta/Vibrato/FX knob groups,
// Mute, Fader, Pan, Clip number) for a single MC-707 track. Track index
// (1-8) doubles as this strip's fixed MIDI channel, matching the real
// device's track-to-channel convention confirmed during M1 (tools/FINDINGS.md).
//
// CC mapping is documented in the class-level table in TrackStripComponent.cpp
// -- see PLAN.md for the full table and which controls have no documented CC
// (Porta Depth, Delay Send: present but disabled, not guessed).
class TrackStripComponent : public juce::Component
{
public:
    using LcdUpdateCallback = std::function<void (const juce::String& paramLabel, int value)>;

    TrackStripComponent (int trackIndex1to8, CCTransmitter& transmitter, juce::Colour accentColour,
                          LcdUpdateCallback onParamChanged);

    void paint (juce::Graphics&) override;
    void resized() override;

    // Called from MainComponent's shared 50Hz timer. Sends CC for any knob
    // that changed since the last poll (Phase 1's throttled-send pattern).
    void pollAndSend();

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::Label label;
        int ccNumber = -1;   // -1 = no documented CC; built visible but disabled
        int lastSent = -1;
        bool big = false;
    };

    void setUpKnob (Knob& k, const juce::String& text, int cc, bool big);
    void pollGroup (Knob* knobs, size_t count);

    int channel;
    CCTransmitter& transmitter;
    juce::Colour accent;
    LcdUpdateCallback onParamChanged;

    juce::Label trackNumberLabel;

    juce::Label filterGroupLabel { {}, "FILTER" };
    juce::Label modGroupLabel { {}, "MOD" };
    juce::Label fxGroupLabel { {}, "FX" };

    std::array<Knob, 3> filterKnobs;  // Cutoff (small), Filter (big), Reso (small)
    std::array<Knob, 4> modKnobs;     // Att (small), Mod (big), Decay (small), Release (single)
    std::array<Knob, 3> portaKnobs;   // Depth, Time, Switch
    std::array<Knob, 3> vibKnobs;     // Depth, Rate, Delay
    std::array<Knob, 4> fxKnobs;      // Delay Send (small), FX (big), Chorus Send (small), Rev Send (single)
    Knob panKnob;

    juce::Slider fader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    int lastSentFaderValue = -1;
    bool muted = false;

    juce::TextButton muteButton { "MUTE" };
    juce::Slider clipSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxBelow };
    juce::Label clipLabel { {}, "CLIP" };
    juce::Label panLabelBelow { {}, "PAN" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackStripComponent)
};
