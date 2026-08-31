#include "FileTransferComponent.h"
#include "../files/SdCardLocator.h"

FileTransferComponent::FileTransferComponent()
{
    instructionsLabel.setText (
        "MIDI FILE TRANSFER\n"
        "\n"
        "1. Put the MC-707 into Storage Mode (device disconnected: hold [SHIFT] + [KNOB ASSIGN] -> "
        "UTILITY -> STORAGE MODE -> [ENTER]), then connect USB. Storage Mode and live MIDI editing "
        "are mutually exclusive -- the Control Panel tab won't see the device while this is active.\n"
        "2. Refresh below to auto-detect the SD card, or choose its drive manually.\n"
        "3. Choose the .mid file(s) to copy. They're placed in ROLAND/GROOVEBOX/MIDI/ on the card.\n"
        "4. Safely eject the drive (Windows' own \"Safely Remove Hardware\") before disconnecting.\n"
        "5. On the device: select the target clip, press [CLIP], choose MIDI FILE, browse to the "
        "file, press [ENTER] to load. All tracks in the file are flattened onto that one clip.",
        juce::dontSendNotification);
    instructionsLabel.setJustificationType (juce::Justification::topLeft);
    instructionsLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    instructionsLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (instructionsLabel);

    driveLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (driveLabel);

    driveValueLabel.setJustificationType (juce::Justification::centredLeft);
    driveValueLabel.setColour (juce::Label::textColourId, juce::Colours::orange);
    addAndMakeVisible (driveValueLabel);

    refreshDriveButton.onClick = [this] { refreshDetectedDrive(); };
    addAndMakeVisible (refreshDriveButton);

    chooseFolderButton.onClick = [this] { chooseFolderManually(); };
    addAndMakeVisible (chooseFolderButton);

    pickFilesButton.onClick = [this] { chooseAndCopyFiles(); };
    addAndMakeVisible (pickFilesButton);

    statusBox.setMultiLine (true);
    statusBox.setReadOnly (true);
    statusBox.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1a1a1a));
    statusBox.setColour (juce::TextEditor::textColourId, juce::Colours::lightgreen);
    addAndMakeVisible (statusBox);

    refreshDetectedDrive();
}

void FileTransferComponent::refreshDetectedDrive()
{
    targetGrooveboxMidiFolder = SdCardLocator::findGrooveboxMidiFolder();

    if (targetGrooveboxMidiFolder != juce::File())
    {
        driveValueLabel.setColour (juce::Label::textColourId, juce::Colours::lightgreen);
        driveValueLabel.setText ("Detected: " + targetGrooveboxMidiFolder.getFullPathName(), juce::dontSendNotification);
    }
    else
    {
        driveValueLabel.setColour (juce::Label::textColourId, juce::Colours::orange);
        driveValueLabel.setText ("Not detected -- put the MC-707 in Storage Mode, Refresh, or choose the drive manually.",
                                  juce::dontSendNotification);
    }
}

void FileTransferComponent::chooseFolderManually()
{
    activeChooser = std::make_unique<juce::FileChooser> ("Select the MC-707 SD card's root drive", juce::File());
    activeChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                 [this] (const juce::FileChooser& chooser)
                                 {
                                     auto folder = chooser.getResult();
                                     if (! folder.exists())
                                         return;

                                     targetGrooveboxMidiFolder = folder.getChildFile ("ROLAND").getChildFile ("GROOVEBOX").getChildFile ("MIDI");
                                     driveValueLabel.setColour (juce::Label::textColourId, juce::Colours::lightgreen);
                                     driveValueLabel.setText ("Using: " + folder.getFullPathName(), juce::dontSendNotification);
                                 });
}

void FileTransferComponent::chooseAndCopyFiles()
{
    activeChooser = std::make_unique<juce::FileChooser> ("Choose .mid file(s) to copy", juce::File(), "*.mid;*.midi");
    activeChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectMultipleItems,
                                 [this] (const juce::FileChooser& chooser) { copyFilesTo (chooser.getResults()); });
}

void FileTransferComponent::copyFilesTo (const juce::Array<juce::File>& files)
{
    if (files.isEmpty())
        return;

    if (targetGrooveboxMidiFolder == juce::File())
    {
        appendStatus ("No target SD card selected -- Refresh or choose a folder first.");
        return;
    }

    if (! targetGrooveboxMidiFolder.createDirectory())
    {
        appendStatus ("FAILED to create " + targetGrooveboxMidiFolder.getFullPathName());
        return;
    }

    for (auto& file : files)
    {
        auto destination = targetGrooveboxMidiFolder.getChildFile (file.getFileName());
        if (file.copyFileTo (destination))
            appendStatus ("Copied " + file.getFileName() + " -> " + destination.getFullPathName());
        else
            appendStatus ("FAILED to copy " + file.getFileName());
    }

    appendStatus ("Done. Safely eject the drive before disconnecting the MC-707.");
}

void FileTransferComponent::appendStatus (const juce::String& line)
{
    statusBox.moveCaretToEnd();
    statusBox.insertTextAtCaret (line + juce::newLine);
}

void FileTransferComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0d0d0d));
}

void FileTransferComponent::resized()
{
    auto area = getLocalBounds().reduced (16);

    instructionsLabel.setBounds (area.removeFromTop (170));
    area.removeFromTop (12);

    auto driveRow = area.removeFromTop (28);
    driveLabel.setBounds (driveRow.removeFromLeft (110));
    chooseFolderButton.setBounds (driveRow.removeFromRight (130));
    driveRow.removeFromRight (8);
    refreshDriveButton.setBounds (driveRow.removeFromRight (80));
    driveRow.removeFromRight (8);
    driveValueLabel.setBounds (driveRow);

    area.removeFromTop (12);
    pickFilesButton.setBounds (area.removeFromTop (32).removeFromLeft (260));

    area.removeFromTop (12);
    statusBox.setBounds (area);
}
