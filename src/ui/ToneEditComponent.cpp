#include "ToneEditComponent.h"
#include "../midi/SysExCodec.h"
#include "../model/ZenCoreAddressing.h"
#include <cmath>

namespace
{
    struct ControlSpec
    {
        const char* section;
        const char* label;
        const char* blockId;
        const char* fieldName;
    };

    // Per tools/FINDINGS.md's "M2 audible pass" sections: the on-device Easy
    // Mode ADSR view maps Attack->Time1, Decay->Time3 (not Time2),
    // Sustain->Level3 (not Level0/1/2), Release->Time4 -- true for AMP and
    // FILTER envelopes alike.
    constexpr std::array<ControlSpec, 41> kControlSpecs { {
        { "AMP",    "ATTACK",  "partial_amp_env", "TVA Env Time1" },
        { "AMP",    "DECAY",   "partial_amp_env", "TVA Env Time3" },
        { "AMP",    "SUSTAIN", "partial_amp_env", "TVA Env Level3" },
        { "AMP",    "RELEASE", "partial_amp_env", "TVA Env Time4" },
        { "AMP",    "PAN",     "tone_partial",    "Pan" },
        { "FILTER", "CUTOFF",  "tone_partial",       "TVF Cutoff Frequency" },
        { "FILTER", "RESO",    "tone_partial",       "TVF Resonance" },
        { "FILTER", "ATTACK",  "partial_filter_env", "TVF Env Time1" },
        { "FILTER", "DECAY",   "partial_filter_env", "TVF Env Time3" },
        { "FILTER", "SUSTAIN", "partial_filter_env", "TVF Env Level3" },
        { "FILTER", "RELEASE", "partial_filter_env", "TVF Env Time4" },
        // Note: this is the ZCore-native partial LFO block, which turned out
        // to be the on-device "LFO 2" tab, not "LFO 1" (which lives in a
        // separate, out-of-scope Model Expansion structure -- see
        // tools/FINDINGS.md "LFO 1 / MDLSYN0 -- resolved as out of scope").
        { "LFO",    "WAVE",    "partial_lfo", "Waveform" },
        { "LFO",    "RATE",    "partial_lfo", "Rate" },
        { "LFO",    "PITCH",   "partial_lfo", "Pitch Depth" },
        { "LFO",    "CUTOFF",  "partial_lfo", "TVF Depth" },
        { "LFO",    "AMP",     "partial_lfo", "TVA Depth" },
        { "LFO",    "PAN",     "partial_lfo", "PAN Depth" },
        // No RELEASE: the on-device Easy Mode PENV screen only shows
        // Depth/Attack/Decay/Sustain -- Time4 wasn't confirmed and isn't
        // reachable via that closed-loop method the way FENV/AENV's was.
        { "PENV",   "DEPTH",   "partial_pitch_env", "Pitch Env Depth" },
        { "PENV",   "ATTACK",  "partial_pitch_env", "Pitch Env Time1" },
        { "PENV",   "DECAY",   "partial_pitch_env", "Pitch Env Time3" },
        { "PENV",   "SUSTAIN", "partial_pitch_env", "Pitch Env Level3" },
        // Gain display formula (raw-1024)/10 = dB; the slider shows the raw
        // 784-1264 value, not the dB number, same simplification as PENV's
        // signed fields above -- a nicer display mapping is future polish.
        // Low/Mid/High Frequency and Mid Q are excluded: their raw-to-Hz/Q
        // encoding isn't determined yet (see data/zencore-map.json's note).
        { "EQ",     "LOW",     "partial_eq", "EQ Low Gain" },
        { "EQ",     "MID",     "partial_eq", "EQ Mid Gain" },
        { "EQ",     "HIGH",    "partial_eq", "EQ High Gain" },
        { "EQ",     "SW",      "partial_eq", "EQ Switch" },
        // tone_mfx has one instance per tone (not per-partial) -- computeAddress
        // still passes the current partialIdx to blockRelativeOffset(), but
        // "tone_mfx" resolves via the single relative_offset branch there,
        // ignoring it, so this is correct regardless of which Partial is selected.
        // Chorus/Reverb Send Level, and the 32 type-dependent MFX Parameter
        // slots, are intentionally excluded -- see data/zencore-map.json's
        // tone_mfx._note.
        { "MFX",    "TYPE",    "tone_mfx", "Type" },
        { "MFX",    "SW",      "tone_mfx", "MFX Switch" },
        // synth_pmt is one instance per tone, like tone_mfx -- Partial
        // selection doesn't affect these two fields' address.
        { "STRUCT", "OSC1-2",  "synth_pmt", "Structure1-2" },
        { "STRUCT", "OSC3-4",  "synth_pmt", "Structure3-4" },
        // Matrix Control: 4 independent "1 source -> up to 4 destinations"
        // slots (tone_partial block, continuing past 0x4F). Only each
        // slot's Source + first Destination/Sens pair is mapped -- matches
        // what the on-device Easy Mode MATRIX screen shows per slot.
        // Source/Destination1 show as raw enum index numbers, not names --
        // see data/zencore-map.json's field "display" text for the lists.
        { "MTX1",   "SRC",     "tone_partial", "Matrix1 Source" },
        { "MTX1",   "DEST",    "tone_partial", "Matrix1 Destination1" },
        { "MTX1",   "DEPTH",   "tone_partial", "Matrix1 Sens1" },
        { "MTX2",   "SRC",     "tone_partial", "Matrix2 Source" },
        { "MTX2",   "DEST",    "tone_partial", "Matrix2 Destination1" },
        { "MTX2",   "DEPTH",   "tone_partial", "Matrix2 Sens1" },
        { "MTX3",   "SRC",     "tone_partial", "Matrix3 Source" },
        { "MTX3",   "DEST",    "tone_partial", "Matrix3 Destination1" },
        { "MTX3",   "DEPTH",   "tone_partial", "Matrix3 Sens1" },
        { "MTX4",   "SRC",     "tone_partial", "Matrix4 Source" },
        { "MTX4",   "DEST",    "tone_partial", "Matrix4 Destination1" },
        { "MTX4",   "DEPTH",   "tone_partial", "Matrix4 Sens1" },
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

    // Section count keeps growing (LFO, PENV, EQ, MFX, ...) and no longer
    // fits a fixed-height window -- everything below the selector bar lives
    // in contentPanel, scrolled via viewport, instead of being added
    // straight to "this".
    contentPanel.controls = &controls;
    contentPanel.sections = &sections;
    viewport.setViewedComponent (&contentPanel, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    // juce::Slider/Label/Component are non-copyable and non-movable, so
    // EditControl can't live directly in a std::vector (even reserve()
    // requires move-construction to be well-formed at compile time) --
    // each one is heap-allocated instead, only the unique_ptr moves.
    controls.reserve (kControlSpecs.size());
    for (auto& spec : kControlSpecs)
    {
        controls.push_back (std::make_unique<EditControl>());
        auto& c = *controls.back();
        c.section = spec.section;
        c.blockId = spec.blockId;
        c.descriptor = parameterMap.findField (c.blockId, spec.fieldName);

        c.label.setText (spec.label, juce::dontSendNotification);
        c.label.setJustificationType (juce::Justification::centred);
        c.label.setFont (juce::Font (juce::FontOptions (11.0f)));
        contentPanel.addAndMakeVisible (c.label);

        if (c.descriptor)
            c.slider.setRange ((double) c.descriptor->rangeLow, (double) c.descriptor->rangeHigh, 1.0);
        else
            c.slider.setEnabled (false); // field not found in the map -- fail visibly, not silently
        contentPanel.addAndMakeVisible (c.slider);
    }

    // Group controls into contiguous runs sharing the same section name
    // (kControlSpecs is already declared grouped by section), each getting
    // one heading label. Generalizes cleanly as more sections are added.
    for (int i = 0; i < (int) controls.size(); ++i)
    {
        if (! sections.empty() && sections.back().name == controls[(size_t) i]->section)
        {
            sections.back().controlCount++;
            continue;
        }

        Section s;
        s.name = controls[(size_t) i]->section;
        s.heading = std::make_unique<juce::Label> (juce::String(), s.name);
        s.heading->setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        s.heading->setColour (juce::Label::textColourId, juce::Colours::orange);
        contentPanel.addAndMakeVisible (*s.heading);
        s.firstControlIndex = i;
        s.controlCount = 1;
        sections.push_back (std::move (s));
    }

    contentPanel.confidenceNote.setText (
        "Only Track 1 / Partial 1 individually hardware-confirmed this session. Other tracks and "
        "partials use the same proven addressing formula but are unconfirmed per-slot.",
        juce::dontSendNotification);
    contentPanel.confidenceNote.setColour (juce::Label::textColourId, juce::Colours::grey);
    contentPanel.confidenceNote.setFont (juce::Font (juce::FontOptions (11.0f)));
    contentPanel.confidenceNote.setJustificationType (juce::Justification::centredLeft);
    contentPanel.addAndMakeVisible (contentPanel.confidenceNote);

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

    if (! transmitter.isOpen())
    {
        setStatus ("Not connected -- select a device on the Control Panel tab.", juce::Colours::orange);
        return;
    }
    if (! transmitter.hasSysExInput())
    {
        setStatus ("Output connected, but no MIDI input matched (" + transmitter.getMatchedInputName() + ").",
                    juce::Colours::orange);
        return;
    }

    setStatus ("Reading...", juce::Colours::yellow);

    for (auto& cPtr : controls)
    {
        auto& c = *cPtr;
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

    for (auto& cPtr : controls)
    {
        auto& c = *cPtr;
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

    area.removeFromTop (20);

    viewport.setBounds (area);
    // Width follows the viewport; height is computed by
    // ContentPanel::resized() itself from section count -- passing the
    // current height here just triggers that pass, which then calls
    // setSize() again with the real height once it knows it.
    contentPanel.setSize (viewport.getWidth(), contentPanel.getHeight());
}

void ToneEditComponent::ContentPanel::resized()
{
    if (controls == nullptr || sections == nullptr)
        return;

    constexpr int controlWidth = 90;
    constexpr int rowHeight = 130;

    int y = 0;
    auto width = getWidth();

    for (auto& section : *sections)
    {
        y += 18;
        section.heading->setBounds (0, y, width, 20);
        y += 20 + 6;

        int x = 0;
        for (int i = section.firstControlIndex; i < section.firstControlIndex + section.controlCount; ++i)
        {
            auto& c = *(*controls)[(size_t) i];
            auto strip = juce::Rectangle<int> (x, y, controlWidth, rowHeight).reduced (10, 0);
            c.label.setBounds (strip.removeFromTop (18));
            strip.removeFromTop (4);
            c.slider.setBounds (strip);
            x += controlWidth;
        }
        y += rowHeight;
    }

    y += 16;
    confidenceNote.setBounds (0, y, width, 32);
    y += 32 + 16;

    setSize (width, y);
}
