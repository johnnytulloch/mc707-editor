#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// Thin shell hosting two tabs: the live Control Panel (ControlPanelComponent)
// and the File Transfer helper (FileTransferComponent). Split into a
// TabbedComponent because the device can't be in Storage Mode (needed for
// file transfer) and respond as a live MIDI device at the same time -- see
// PLAN.md.
class MainComponent : public juce::Component
{
public:
    MainComponent();

    void resized() override;

private:
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
