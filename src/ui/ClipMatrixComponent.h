#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include <memory>

// Clip Copy Matrix: an 8-track x 16-clip grid for assigning local .mid files
// to intended (track, clip) slots, then bulk-copying them to the MC-707's SD
// card with a T{track}-C{clip}_ filename prefix. This only prepares and
// copies files -- there is no remote way to trigger the on-device "load into
// clip" step itself (confirmed against the official MC-707 update docs), so
// that step is still manual, once per clip. See PLAN.md.
// A grid cell button that also reports right-clicks, used for the in-app
// Copy/Paste context menu (left-click keeps TextButton's normal onClick
// behaviour -- opens the file chooser -- unchanged).
class ClipCellButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onRightClick)
            onRightClick();
        else
            juce::TextButton::mouseDown (e);
    }
};

class ClipMatrixComponent : public juce::Component
{
public:
    ClipMatrixComponent();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int kNumTracks = 8;
    static constexpr int kNumClips = 16;

    void refreshDetectedDrive();
    void chooseFolderManually();
    void assignCell (int track, int clip);
    void showCellContextMenu (int track, int clip);
    void clearAll();
    void copyAllAssigned();
    void updateCellButtonText (int track, int clip);
    void appendStatus (const juce::String& line);

    juce::File targetGrooveboxMidiFolder;
    juce::File clipboardFile; // set via a cell's right-click "Copy", used by "Paste"
    std::array<std::array<juce::File, kNumClips>, kNumTracks> assignments;

    juce::Label instructionsLabel;

    juce::Label driveLabel { {}, "Target SD card:" };
    juce::Label driveValueLabel;
    juce::TextButton refreshDriveButton { "Refresh" };
    juce::TextButton chooseFolderButton { "Choose folder..." };

    juce::TextButton clearAllButton { "Clear All" };
    juce::TextButton copyAllButton { "Copy All Assigned" };

    std::array<juce::Label, kNumTracks> trackHeaderLabels;
    std::array<juce::Label, kNumClips> clipHeaderLabels;
    std::array<std::array<std::unique_ptr<ClipCellButton>, kNumClips>, kNumTracks> cellButtons;

    juce::TextEditor statusBox;

    std::unique_ptr<juce::FileChooser> activeChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipMatrixComponent)
};
