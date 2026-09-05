#include "ToneEditComponent.h"
#include "../midi/SysExCodec.h"
#include "../model/ZenCoreAddressing.h"
#include "../model/ToneSnapshot.h"
#include <cmath>

namespace
{
    struct ControlSpec
    {
        const char* section;
        const char* label;
        const char* blockId;
        // "{P}" is replaced with the selected partial number (1-4) at
        // lookup time, for fields whose *name* (not just their block's
        // relative offset) varies per partial -- see EditControl::fieldNameTemplate.
        const char* fieldName;
    };

    // Per tools/FINDINGS.md's "M2 audible pass" sections: the on-device Easy
    // Mode ADSR view maps Attack->Time1, Decay->Time3 (not Time2),
    // Sustain->Level3 (not Level0/1/2), Release->Time4 -- true for AMP and
    // FILTER envelopes alike.
    constexpr std::array<ControlSpec, 49> kControlSpecs { {
        // OSC screen, page 1 (SW/COARSE/FINE/VOL) -- SW is tone_pmt's
        // per-partial "Partial N Switch" (name varies by partial, not just
        // offset, hence "{P}"); COARSE/FINE/VOL are tone_partial fields
        // already used elsewhere (Coarse Tune/Fine Tune/Level), just shown
        // under different labels on this particular screen.
        { "OSC",    "SW",      "tone_pmt",      "Partial {P} Switch" },
        { "OSC",    "COARSE",  "tone_partial",  "Coarse Tune" },
        { "OSC",    "FINE",    "tone_partial",  "Fine Tune" },
        { "OSC",    "VOL",     "tone_partial",  "Level" },
        // OSC screen, page 2 (OSC/FORM/PW/PWM) -- the analog-modeling
        // oscillator layer, in ZCore's own per-tone address space (unlike
        // the separate out-of-scope MDLSYN0 structure).
        { "OSC",    "TYPE",    "synth_partial", "OSC Type" },
        { "OSC",    "FORM",    "synth_partial", "VA Waveform" },
        { "OSC",    "PW",      "synth_partial", "Pulse Width" },
        { "OSC",    "PWM",     "synth_partial", "PWM Depth" },
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

    clipBox.addItem ("Track Sound", 1);
    for (int c = 1; c <= 16; ++c)
        clipBox.addItem ("Clip " + juce::String (c), c + 1);
    clipBox.setSelectedId (1, juce::dontSendNotification);
    clipBox.onChange = [this] { refreshAll(); };
    addAndMakeVisible (clipBox);
    addAndMakeVisible (clipLabel);

    refreshButton.onClick = [this] { refreshAll(); };
    addAndMakeVisible (refreshButton);

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);
    setStatus ("Not read yet -- hit Refresh once connected.", juce::Colours::grey);

    addAndMakeVisible (nameLabel);
    // Not the 50Hz poll-and-send pattern the sliders use -- a text field has no
    // natural "current value to diff against last-sent", and writing on every
    // keystroke would spam SysEx mid-edit. Commit on Enter or on losing focus instead.
    nameEditor.onReturnKey = [this] { writeName(); };
    nameEditor.onFocusLost = [this] { writeName(); };
    addAndMakeVisible (nameEditor);

    addAndMakeVisible (categoryLabel);
    if (auto categoryField = parameterMap.findField ("tone_common", "Category"))
        categorySlider.setRange ((double) categoryField->rangeLow, (double) categoryField->rangeHigh, 1.0);
    addAndMakeVisible (categorySlider);

    captureButton.onClick = [this] { captureSnapshot(); };
    addAndMakeVisible (captureButton);
    writeButton.onClick = [this] { writeSnapshot(); };
    addAndMakeVisible (writeButton);
    saveButton.onClick = [this] { saveSnapshotToFile(); };
    addAndMakeVisible (saveButton);
    loadButton.onClick = [this] { loadSnapshotFromFile(); };
    addAndMakeVisible (loadButton);

    snapshotInfoLabel.setJustificationType (juce::Justification::centredLeft);
    snapshotInfoLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (snapshotInfoLabel);
    setSnapshotStatus ("No tone captured yet.", juce::Colours::grey);
    addAndMakeVisible (snapshotStatusLabel);

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
        juce::String fieldName (spec.fieldName);
        if (fieldName.contains ("{P}"))
        {
            c.fieldNameTemplate = fieldName;
            c.descriptor = resolveDescriptor (c); // resolve once against the default Partial 1 for initial range setup
        }
        else
        {
            c.descriptor = parameterMap.findField (c.blockId, fieldName);
        }

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
        "Track 1 (all 4 Partials, all sections) and Tracks 4/8 (AMP section) confirmed live through "
        "this app. All other tracks use the identical addressing formula, verified independently on "
        "Tracks 4/8 -- the two tracks needing the 7-bit-carry correction -- but are unconfirmed per-slot.",
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

uint32_t ToneEditComponent::computeSlotAddress() const
{
    auto trackIdx = trackBox.getSelectedId() - 1;

    // clipBox item 1 = "Track Sound" (clipIdx = nullopt); items 2-17 = Clip
    // 1-16 (clipIdx = 0-15).
    auto clipItemId = clipBox.getSelectedId();
    std::optional<int> clipIdx = clipItemId <= 1 ? std::nullopt : std::optional<int> (clipItemId - 2);

    return ZenCoreAddressing::getSlotAddress (
        parameterMap.trackBase(), parameterMap.toneAlignment(), trackIdx, clipIdx);
}

uint32_t ToneEditComponent::computeAddress (const juce::String& blockId, const FieldDescriptor& field) const
{
    auto partialIdx = partialBox.getSelectedId() - 1;
    auto slotAddress = computeSlotAddress();

    // Block-relative-offset and field-offset additions are safe with plain
    // addition (proven this session -- every block/field offset lands in a
    // byte that starts at 0 in every slot address and never nears 0x80).
    // Only the slot address itself needed the 7-bit-carry treatment above.
    auto blockBase = slotAddress + (uint32_t) parameterMap.blockRelativeOffset (blockId, partialIdx);
    return blockBase + (uint32_t) field.offsetWithinBlock;
}

std::optional<FieldDescriptor> ToneEditComponent::resolveDescriptor (EditControl& c) const
{
    if (c.fieldNameTemplate.isEmpty())
        return c.descriptor;

    auto partialNumber = partialBox.getSelectedId(); // 1-4
    auto name = c.fieldNameTemplate.replace ("{P}", juce::String (partialNumber));
    return parameterMap.findField (c.blockId, name);
}

void ToneEditComponent::setStatus (const juce::String& text, juce::Colour colour)
{
    statusLabel.setText (text, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, colour);
}

void ToneEditComponent::refreshNameAndCategory()
{
    auto nameField = parameterMap.findField ("tone_common", "Name");
    auto categoryField = parameterMap.findField ("tone_common", "Category");
    if (! nameField || ! categoryField)
        return;

    auto nameAddress = computeAddress ("tone_common", *nameField);
    transmitter.sendSysExRead (nameAddress, nameField->size, [this] (bool ok, std::vector<uint8_t> data)
    {
        if (! ok)
        {
            setStatus ("Name read failed -- no reply.", juce::Colours::orange);
            return;
        }
        nameEditor.setText (SysExCodec::decodeAscii (data), juce::dontSendNotification);
        nameInitialized = true;
    });

    auto categoryAddress = computeAddress ("tone_common", *categoryField);
    transmitter.sendSysExRead (categoryAddress, categoryField->size, [this] (bool ok, std::vector<uint8_t> data)
    {
        if (! ok)
        {
            setStatus ("Category read failed -- no reply.", juce::Colours::orange);
            return;
        }
        auto value = SysExCodec::decodeUint7 (data);
        categorySlider.setValue ((double) value, juce::dontSendNotification);
        categoryLastSent = value;
    });
}

void ToneEditComponent::writeName()
{
    if (! nameInitialized) // don't write before the first real read completes
        return;

    auto nameField = parameterMap.findField ("tone_common", "Name");
    if (! nameField)
        return;

    auto address = computeAddress ("tone_common", *nameField);
    auto data = SysExCodec::encodeAscii (nameEditor.getText(), nameField->size);
    transmitter.sendSysExWrite (address, data);
}

void ToneEditComponent::setSnapshotStatus (const juce::String& text, juce::Colour colour)
{
    snapshotStatusLabel.setText (text, juce::dontSendNotification);
    snapshotStatusLabel.setColour (juce::Label::textColourId, colour);
}

void ToneEditComponent::captureSnapshot()
{
    if (! parameterMap.isValid())
    {
        setSnapshotStatus ("Parameter map failed to load.", juce::Colours::red);
        return;
    }
    if (! transmitter.isSysExReady())
    {
        setSnapshotStatus ("Not connected -- select a device on the Control Panel tab.", juce::Colours::orange);
        return;
    }

    setSnapshotStatus ("Capturing...", juce::Colours::yellow);
    auto slotAddress = computeSlotAddress();

    readToneSnapshot (transmitter, parameterMap, slotAddress, [this] (bool ok, ToneSnapshot snapshot)
    {
        if (! ok)
        {
            setSnapshotStatus ("Capture failed -- a read timed out.", juce::Colours::orange);
            return;
        }

        capturedSnapshot = std::move (snapshot);
        snapshotInfoLabel.setText ("Captured: " + capturedSnapshot->name.trim() + " (Cat " + juce::String (capturedSnapshot->category) + ")",
                                     juce::dontSendNotification);
        setSnapshotStatus ("Captured " + juce::String ((int) capturedSnapshot->chunks.size()) + " blocks.", juce::Colours::lightgreen);
    });
}

void ToneEditComponent::writeSnapshot()
{
    if (! capturedSnapshot)
    {
        setSnapshotStatus ("Nothing captured yet -- Capture or Load a tone first.", juce::Colours::orange);
        return;
    }
    if (! transmitter.isOpen())
    {
        setSnapshotStatus ("Not connected -- select a device on the Control Panel tab.", juce::Colours::orange);
        return;
    }

    auto slotAddress = computeSlotAddress();
    writeToneSnapshot (transmitter, parameterMap, slotAddress, *capturedSnapshot, [this] (bool ok)
    {
        setSnapshotStatus (ok ? "Written -- hit Refresh to see it on this selection."
                              : "Write failed -- nothing was captured to send.",
                            ok ? juce::Colours::lightgreen : juce::Colours::orange);
    });
}

void ToneEditComponent::saveSnapshotToFile()
{
    if (! capturedSnapshot)
    {
        setSnapshotStatus ("Nothing captured yet -- Capture a tone first.", juce::Colours::orange);
        return;
    }

    auto suggestedName = capturedSnapshot->name.trim().isNotEmpty() ? capturedSnapshot->name.trim() : juce::String ("tone");
    activeChooser = std::make_unique<juce::FileChooser> ("Save tone as...",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (suggestedName + ".tone.json"),
        "*.json");
    activeChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (file == juce::File {})
                return;

            auto json = juce::JSON::toString (toneSnapshotToVar (*capturedSnapshot));
            if (file.replaceWithText (json))
                setSnapshotStatus ("Saved to " + file.getFileName() + ".", juce::Colours::lightgreen);
            else
                setSnapshotStatus ("Failed to write " + file.getFileName() + ".", juce::Colours::orange);
        });
}

void ToneEditComponent::loadSnapshotFromFile()
{
    activeChooser = std::make_unique<juce::FileChooser> ("Load tone from file...",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.json");
    activeChooser->launchAsync (juce::FileBrowserComponent::openMode,
        [this] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (! file.existsAsFile())
                return;

            auto parsed = juce::JSON::parse (file.loadFileAsString());
            auto snapshot = toneSnapshotFromVar (parsed);
            if (! snapshot)
            {
                setSnapshotStatus ("Couldn't parse " + file.getFileName() + " as a tone file.", juce::Colours::orange);
                return;
            }

            capturedSnapshot = std::move (snapshot);
            snapshotInfoLabel.setText ("Loaded: " + capturedSnapshot->name.trim() + " (Cat " + juce::String (capturedSnapshot->category) + ")",
                                         juce::dontSendNotification);
            setSnapshotStatus ("Loaded " + juce::String ((int) capturedSnapshot->chunks.size()) + " blocks from "
                                    + file.getFileName() + ".",
                                juce::Colours::lightgreen);
        });
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

    nameInitialized = false; // don't let a commit fire on stale text while re-reading
    refreshNameAndCategory();

    for (auto& cPtr : controls)
    {
        auto& c = *cPtr;
        c.initialized = false; // don't let the timer write stale values while re-reading

        // Re-resolves fields whose name (not just their block's per-partial
        // offset) depends on the selected partial, e.g. tone_pmt's "Partial
        // N Switch" -- keeps c.descriptor in sync for timerCallback() too.
        c.descriptor = resolveDescriptor (c);
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

    if (categoryLastSent != -1) // -1 sentinel: no real read has completed yet
    {
        auto value = (int) std::round (categorySlider.getValue());
        if (value != categoryLastSent)
        {
            categoryLastSent = value;
            if (auto categoryField = parameterMap.findField ("tone_common", "Category"))
            {
                auto address = computeAddress ("tone_common", *categoryField);
                transmitter.sendSysExWrite (address, { SysExCodec::encodeUint7 (value) });
            }
        }
    }

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
    clipLabel.setBounds (selectorArea.removeFromLeft (40));
    clipBox.setBounds (selectorArea.removeFromLeft (110));
    selectorArea.removeFromLeft (16);
    refreshButton.setBounds (selectorArea.removeFromLeft (90));
    selectorArea.removeFromLeft (12);
    statusLabel.setBounds (selectorArea);

    area.removeFromTop (8);
    auto identityArea = area.removeFromTop (28);
    nameLabel.setBounds (identityArea.removeFromLeft (75));
    nameEditor.setBounds (identityArea.removeFromLeft (180));
    identityArea.removeFromLeft (16);
    categoryLabel.setBounds (identityArea.removeFromLeft (65));
    categorySlider.setBounds (identityArea.removeFromLeft (150));

    area.removeFromTop (8);
    auto snapshotArea = area.removeFromTop (28);
    captureButton.setBounds (snapshotArea.removeFromLeft (110));
    snapshotArea.removeFromLeft (10);
    writeButton.setBounds (snapshotArea.removeFromLeft (150));
    snapshotArea.removeFromLeft (10);
    saveButton.setBounds (snapshotArea.removeFromLeft (80));
    snapshotArea.removeFromLeft (6);
    loadButton.setBounds (snapshotArea.removeFromLeft (80));
    snapshotArea.removeFromLeft (12);
    snapshotInfoLabel.setBounds (snapshotArea.removeFromLeft (juce::jmax (0, snapshotArea.getWidth() / 2)));
    snapshotArea.removeFromLeft (10);
    snapshotStatusLabel.setBounds (snapshotArea);

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
