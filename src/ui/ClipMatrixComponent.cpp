#include "ClipMatrixComponent.h"
#include "../files/SdCardLocator.h"

namespace
{
    juce::String shortenName (const juce::String& name, int maxChars)
    {
        if (name.length() <= maxChars)
            return name;
        return name.substring (0, maxChars - 1) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
    }

    // Strips a leading "T<digits>-C<digits>_" prefix if present. The SD card's Storage Mode
    // folder is easy to end up back in via the file chooser's remembered last-used directory, so
    // a file this tool already copied/renamed can get picked as the *source* for another cell --
    // without this, re-copying it would stack another prefix on top instead of replacing it.
    juce::String stripExistingClipPrefix (const juce::String& fileName)
    {
        if (! fileName.startsWithChar ('T'))
            return fileName;

        int i = 1;
        while (i < fileName.length() && juce::CharacterFunctions::isDigit (fileName[i]))
            ++i;
        if (i == 1 || i >= fileName.length() || fileName[i] != '-')
            return fileName;
        ++i;
        if (i >= fileName.length() || fileName[i] != 'C')
            return fileName;
        ++i;
        auto clipDigitsStart = i;
        while (i < fileName.length() && juce::CharacterFunctions::isDigit (fileName[i]))
            ++i;
        if (i == clipDigitsStart || i >= fileName.length() || fileName[i] != '_')
            return fileName;

        return fileName.substring (i + 1);
    }
}

ClipMatrixComponent::ClipMatrixComponent()
{
    instructionsLabel.setText (
        "CLIP COPY MATRIX -- prepares files, does not load them\n"
        "Assign a local .mid file to each (track, clip) slot below, then Copy All Assigned to send "
        "them all to the SD card at once, renamed \"T{track}-C{clip}_name.mid\" so each is "
        "identifiable in the device's flat file browser. The on-device load-into-clip step "
        "([CLIP] -> MIDI FILE -> browse -> [ENTER]) is still manual, once per clip -- there is no "
        "remote way to trigger it. Right-click a filled cell to Copy it, right-click any cell to "
        "Paste -- use this to reuse the same file across multiple clips without re-browsing.",
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

    clearAllButton.onClick = [this] { clearAll(); };
    addAndMakeVisible (clearAllButton);

    copyAllButton.onClick = [this] { copyAllAssigned(); };
    addAndMakeVisible (copyAllButton);

    for (int t = 0; t < kNumTracks; ++t)
    {
        trackHeaderLabels[(size_t) t].setText ("T" + juce::String (t + 1), juce::dontSendNotification);
        trackHeaderLabels[(size_t) t].setJustificationType (juce::Justification::centred);
        addAndMakeVisible (trackHeaderLabels[(size_t) t]);
    }

    for (int c = 0; c < kNumClips; ++c)
    {
        clipHeaderLabels[(size_t) c].setText ("C" + juce::String (c + 1), juce::dontSendNotification);
        clipHeaderLabels[(size_t) c].setJustificationType (juce::Justification::centred);
        clipHeaderLabels[(size_t) c].setFont (juce::Font (juce::FontOptions (10.0f)));
        addAndMakeVisible (clipHeaderLabels[(size_t) c]);
    }

    for (int t = 0; t < kNumTracks; ++t)
    {
        for (int c = 0; c < kNumClips; ++c)
        {
            auto button = std::make_unique<ClipCellButton> ("+");
            button->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff262626));
            auto* buttonPtr = button.get();
            buttonPtr->onClick = [this, t, c] { assignCell (t, c); };
            buttonPtr->onRightClick = [this, t, c] { showCellContextMenu (t, c); };
            addAndMakeVisible (*buttonPtr);
            cellButtons[(size_t) t][(size_t) c] = std::move (button);
        }
    }

    statusBox.setMultiLine (true);
    statusBox.setReadOnly (true);
    statusBox.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1a1a1a));
    statusBox.setColour (juce::TextEditor::textColourId, juce::Colours::lightgreen);
    addAndMakeVisible (statusBox);

    refreshDetectedDrive();
}

void ClipMatrixComponent::refreshDetectedDrive()
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

void ClipMatrixComponent::chooseFolderManually()
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

void ClipMatrixComponent::showCellContextMenu (int track, int clip)
{
    auto& file = assignments[(size_t) track][(size_t) clip];

    juce::PopupMenu menu;
    if (file != juce::File())
        menu.addItem (1, "Copy \"" + file.getFileName() + "\"");
    if (clipboardFile != juce::File())
        menu.addItem (2, "Paste \"" + clipboardFile.getFileName() + "\" here");
    if (file != juce::File())
        menu.addItem (3, "Clear this cell");

    if (menu.getNumItems() == 0)
        return;

    menu.showMenuAsync (juce::PopupMenu::Options(),
                         [this, track, clip] (int result)
                         {
                             if (result == 1)
                             {
                                 clipboardFile = assignments[(size_t) track][(size_t) clip];
                             }
                             else if (result == 2)
                             {
                                 assignments[(size_t) track][(size_t) clip] = clipboardFile;
                                 updateCellButtonText (track, clip);
                             }
                             else if (result == 3)
                             {
                                 assignments[(size_t) track][(size_t) clip] = juce::File();
                                 updateCellButtonText (track, clip);
                             }
                         });
}

void ClipMatrixComponent::assignCell (int track, int clip)
{
    activeChooser = std::make_unique<juce::FileChooser> ("Choose a .mid file for T" + juce::String (track + 1)
                                                               + "-C" + juce::String (clip + 1),
                                                           juce::File(), "*.mid;*.midi");
    activeChooser->launchAsync (juce::FileBrowserComponent::openMode,
                                 [this, track, clip] (const juce::FileChooser& chooser)
                                 {
                                     auto file = chooser.getResult();
                                     if (! file.existsAsFile())
                                         return;

                                     assignments[(size_t) track][(size_t) clip] = file;
                                     updateCellButtonText (track, clip);
                                 });
}

void ClipMatrixComponent::clearAll()
{
    for (int t = 0; t < kNumTracks; ++t)
    {
        for (int c = 0; c < kNumClips; ++c)
        {
            assignments[(size_t) t][(size_t) c] = juce::File();
            updateCellButtonText (t, c);
        }
    }
    appendStatus ("Cleared all assignments.");
}

void ClipMatrixComponent::updateCellButtonText (int track, int clip)
{
    auto& file = assignments[(size_t) track][(size_t) clip];
    auto* button = cellButtons[(size_t) track][(size_t) clip].get();

    if (file == juce::File())
    {
        button->setButtonText ("+");
        button->setTooltip ({});
        button->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff262626));
    }
    else
    {
        button->setButtonText (shortenName (file.getFileNameWithoutExtension(), 8));
        button->setTooltip (file.getFullPathName());
        button->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2f5c3a));
    }
}

void ClipMatrixComponent::copyAllAssigned()
{
    if (targetGrooveboxMidiFolder == juce::File())
    {
        appendStatus ("No target SD card selected -- Refresh or choose a folder first.");
        return;
    }

    int assignedCount = 0, copiedCount = 0, failedCount = 0;

    for (int t = 0; t < kNumTracks; ++t)
    {
        for (int c = 0; c < kNumClips; ++c)
        {
            auto& file = assignments[(size_t) t][(size_t) c];
            if (file == juce::File())
                continue;

            ++assignedCount;

            if (! targetGrooveboxMidiFolder.createDirectory())
            {
                appendStatus ("FAILED to create " + targetGrooveboxMidiFolder.getFullPathName());
                ++failedCount;
                continue;
            }

            auto destName = "T" + juce::String (t + 1) + "-C" + juce::String (c + 1) + "_"
                             + stripExistingClipPrefix (file.getFileName());
            auto destination = targetGrooveboxMidiFolder.getChildFile (destName);

            if (file.copyFileTo (destination))
            {
                appendStatus ("Copied " + file.getFileName() + " -> " + destination.getFullPathName());
                ++copiedCount;
            }
            else
            {
                appendStatus ("FAILED to copy " + file.getFileName());
                ++failedCount;
            }
        }
    }

    appendStatus (juce::String (assignedCount) + " assigned, " + juce::String (copiedCount) + " copied, "
                  + juce::String (failedCount) + " failed. Safely eject the drive before disconnecting.");
}

void ClipMatrixComponent::appendStatus (const juce::String& line)
{
    statusBox.moveCaretToEnd();
    statusBox.insertTextAtCaret (line + juce::newLine);
}

void ClipMatrixComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0d0d0d));
}

void ClipMatrixComponent::resized()
{
    auto area = getLocalBounds().reduced (16);

    instructionsLabel.setBounds (area.removeFromTop (92));
    area.removeFromTop (8);

    auto driveRow = area.removeFromTop (28);
    driveLabel.setBounds (driveRow.removeFromLeft (110));
    chooseFolderButton.setBounds (driveRow.removeFromRight (130));
    driveRow.removeFromRight (8);
    refreshDriveButton.setBounds (driveRow.removeFromRight (80));
    driveRow.removeFromRight (8);
    driveValueLabel.setBounds (driveRow);

    area.removeFromTop (8);
    auto actionRow = area.removeFromTop (28);
    copyAllButton.setBounds (actionRow.removeFromLeft (160));
    actionRow.removeFromLeft (8);
    clearAllButton.setBounds (actionRow.removeFromLeft (100));

    area.removeFromTop (10);

    constexpr int trackLabelWidth = 40;
    constexpr int rowHeight = 26;
    constexpr int headerHeight = 18;

    auto gridArea = area.removeFromTop (headerHeight + kNumTracks * rowHeight);
    auto headerRow = gridArea.removeFromTop (headerHeight);
    headerRow.removeFromLeft (trackLabelWidth);
    auto cellWidth = headerRow.getWidth() / kNumClips;
    for (int c = 0; c < kNumClips; ++c)
        clipHeaderLabels[(size_t) c].setBounds (headerRow.removeFromLeft (cellWidth));

    for (int t = 0; t < kNumTracks; ++t)
    {
        auto row = gridArea.removeFromTop (rowHeight);
        trackHeaderLabels[(size_t) t].setBounds (row.removeFromLeft (trackLabelWidth));
        for (int c = 0; c < kNumClips; ++c)
            cellButtons[(size_t) t][(size_t) c]->setBounds (row.removeFromLeft (cellWidth).reduced (1));
    }

    area.removeFromTop (10);
    statusBox.setBounds (area);
}
