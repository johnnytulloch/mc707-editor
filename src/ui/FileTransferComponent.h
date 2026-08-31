#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// MIDI-file-to-SD-card copy helper (brief §3 / M5). The MC-707 has no
// remote/MIDI-triggerable "import this file" command -- confirmed against
// the official Ver.1.30 update documentation (loading an SMF into a clip is
// a manual on-device menu action). This component only automates the file
// copy into ROLAND/GROOVEBOX/MIDI/ on the device's SD card (mounted via the
// device's Storage Mode) and shows the exact on-device steps to finish the
// import. No MIDI/SysEx involved here at all.
class FileTransferComponent : public juce::Component
{
public:
    FileTransferComponent();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refreshDetectedDrive();
    void chooseFolderManually();
    void chooseAndCopyFiles();
    void copyFilesTo (const juce::Array<juce::File>& files);
    void appendStatus (const juce::String& line);

    juce::File targetGrooveboxMidiFolder; // <drive>/ROLAND/GROOVEBOX/MIDI, empty if none selected

    juce::Label instructionsLabel;

    juce::Label driveLabel { {}, "Target SD card:" };
    juce::Label driveValueLabel;
    juce::TextButton refreshDriveButton { "Refresh" };
    juce::TextButton chooseFolderButton { "Choose folder..." };

    juce::TextButton pickFilesButton { "Choose .mid file(s) to copy..." };

    juce::TextEditor statusBox;

    std::unique_ptr<juce::FileChooser> activeChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FileTransferComponent)
};
