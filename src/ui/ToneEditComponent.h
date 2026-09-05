#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>
#include <optional>
#include <vector>
#include "../midi/CCTransmitter.h"
#include "../model/ParameterMap.h"
#include "../model/ToneSnapshot.h"

// First real SysEx-editing panel in the app (M3 pilot). Edits fields
// closed-loop confirmed against real hardware this session (see
// tools/FINDINGS.md's "M2 audible pass" sections): AMP (per-partial Pan,
// Amp Envelope ADSR) and FILTER (Cutoff, Resonance, Filter Envelope ADSR).
// Addresses a track's own "track sound" slot (not a specific clip tone) --
// matches everything validated so far; clip-tone editing is a later
// extension. Grouped into labelled sections that grow as more fields get
// confirmed (LFO, PENV, EQ...) -- see kControlSpecs in the .cpp.
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
        juce::String section;
        juce::String blockId;
        // Most fields have a fixed name and their per-partial variation is
        // handled by the block's own relative_offset_per_partial. A few
        // (e.g. tone_pmt's per-partial "Partial N Switch") instead vary by
        // *name* per partial -- fieldNameTemplate holds "{P}" for those,
        // substituted with the selected partial (1-4) on every lookup;
        // descriptor is then re-resolved instead of cached from construction.
        juce::String fieldNameTemplate;
        std::optional<FieldDescriptor> descriptor;
        int lastSent = -1;
        bool initialized = false; // guards against writing before the first read completes
    };

    struct Section
    {
        juce::String name;
        std::unique_ptr<juce::Label> heading;
        int firstControlIndex = 0;
        int controlCount = 0;
    };

    // Everything below the track/partial selector bar lives here, inside a
    // Viewport, so the section count can keep growing (LFO, PENV, EQ, MFX...)
    // without overflowing the window -- it scrolls instead of clipping.
    struct ContentPanel : public juce::Component
    {
        void resized() override;
        std::vector<std::unique_ptr<EditControl>>* controls = nullptr;
        std::vector<Section>* sections = nullptr;
        juce::Label confidenceNote;
    };

    void timerCallback() override; // poll-and-send-on-change, matches TrackStripComponent
    void refreshAll();
    // tone_common's Name/Category -- "which tone is this", not a detail to bury in the
    // scrolling section grid, so these live as dedicated header-row controls rather
    // than going through kControlSpecs/EditControl (Name is ascii -- the only one in
    // the whole map -- so it doesn't fit EditControl's Slider-only shape anyway).
    void refreshNameAndCategory();
    void writeName();
    void setStatus (const juce::String& text, juce::Colour colour);
    // Track/Clip selection -> a full slot address (track sound, or a
    // specific clip's tone if the Clip selector isn't set to "Track Sound").
    uint32_t computeSlotAddress() const;
    uint32_t computeAddress (const juce::String& blockId, const FieldDescriptor& field) const;
    // Resolves fieldNameTemplate against the currently-selected partial, if set.
    std::optional<FieldDescriptor> resolveDescriptor (EditControl& c) const;

    void captureSnapshot();
    void writeSnapshot();
    void saveSnapshotToFile();
    void loadSnapshotFromFile();
    void setSnapshotStatus (const juce::String& text, juce::Colour colour);

    CCTransmitter& transmitter;
    ParameterMap parameterMap;

    juce::Label titleLabel { {}, "TONE EDIT" };
    juce::Label subtitleLabel { {}, "Track sound or a clip's tone, M3 pilot \xE2\x80\x94 sections grow as more fields are confirmed" };

    juce::Label trackLabel { {}, "Track:" };
    juce::ComboBox trackBox;
    juce::Label partialLabel { {}, "Partial:" };
    juce::ComboBox partialBox;
    // Item 1 = "Track Sound" (clipIdx = nullopt); items 2-17 = Clip 1-16
    // (clipIdx = 0-15). Affects every read/write in this component, not
    // just Snapshot -- closes the "clip-tone editing is a later extension"
    // gap noted in this class's own header comment.
    juce::Label clipLabel { {}, "Clip:" };
    juce::ComboBox clipBox;
    juce::TextButton refreshButton { "Refresh" };
    juce::Label statusLabel;

    juce::Label nameLabel { {}, "Tone Name:" };
    juce::TextEditor nameEditor;
    juce::Label categoryLabel { {}, "Category:" };
    juce::Slider categorySlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxLeft };
    // Shared by Name and Category -- both come from the same refreshNameAndCategory()
    // read, so they become valid at the same moment. Mirrors EditControl's own
    // initialized/lastSent pair, kept separate since these two fields aren't EditControls.
    bool nameInitialized = false;
    int categoryLastSent = -1;

    juce::TextButton captureButton { "Capture Tone" };
    juce::TextButton writeButton { "Write To Selection" };
    juce::TextButton saveButton { "Save..." };
    juce::TextButton loadButton { "Load..." };
    juce::Label snapshotInfoLabel;
    juce::Label snapshotStatusLabel;
    std::optional<ToneSnapshot> capturedSnapshot;
    std::unique_ptr<juce::FileChooser> activeChooser;

    juce::Viewport viewport;
    ContentPanel contentPanel;

    // EditControl holds a juce::Slider/Label, which are non-copyable and
    // non-movable -- std::vector<EditControl> can't work even with
    // reserve()+emplace_back() (the container's move/copy requirements are
    // checked at compile time regardless of whether they're ever hit at
    // runtime), hence unique_ptr per element.
    std::vector<std::unique_ptr<EditControl>> controls;
    std::vector<Section> sections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneEditComponent)
};
