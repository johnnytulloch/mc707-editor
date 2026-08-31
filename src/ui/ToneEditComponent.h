#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include <optional>
#include "../midi/CCTransmitter.h"
#include "../model/ParameterMap.h"

// First real SysEx-editing panel in the app (M3 pilot). Edits the AMP
// section fields closed-loop confirmed against real hardware this session
// (tools/FINDINGS.md, "M2 audible pass, round 1"): per-partial Pan, and the
// Amp Envelope's Attack/Decay/Sustain/Release. Addresses a track's own
// "track sound" slot (not a specific clip tone) -- matches everything
// validated so far; clip-tone editing is a later extension.
//
// Field offsets/sizes/ranges come from ParameterMap (data/zencore-map.json),
// never hardcoded here -- see CLAUDE.md. Shares the app's one MIDI
// connection (CCTransmitter&, owned by MainComponent) rather than opening
// its own -- MC-707's output port is exclusive on Windows.
class ToneEditComponent : public juce::Component,
                           private juce::Timer
{
public:
    explicit ToneEditComponent (CCTransmitter& transmitter);
    ~ToneEditComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct EditControl
    {
        juce::Slider slider { juce::Slider::LinearVertical, juce::Slider::TextBoxBelow };
        juce::Label label;
        juce::String blockId;
        std::optional<FieldDescriptor> descriptor;
        int lastSent = -1;
        bool initialized = false; // guards against writing before the first read completes
    };

    void timerCallback() override; // poll-and-send-on-change, matches TrackStripComponent
    void refreshAll();
    void setStatus (const juce::String& text, juce::Colour colour);
    uint32_t computeAddress (const juce::String& blockId, const FieldDescriptor& field) const;

    CCTransmitter& transmitter;
    ParameterMap parameterMap;

    juce::Label titleLabel { {}, "TONE EDIT" };
    juce::Label subtitleLabel { {}, "AMP section \xE2\x80\x94 track sound, M3 pilot" };

    juce::Label trackLabel { {}, "Track:" };
    juce::ComboBox trackBox;
    juce::Label partialLabel { {}, "Partial:" };
    juce::ComboBox partialBox;
    juce::TextButton refreshButton { "Refresh" };
    juce::Label statusLabel;
    juce::Label confidenceNote;

    std::array<EditControl, 5> controls; // Attack, Decay, Sustain, Release, Pan

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneEditComponent)
};
