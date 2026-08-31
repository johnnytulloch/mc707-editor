#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../midi/CCTransmitter.h"

// Thin shell hosting the app's tabs (Control Panel / File Transfer / Clip
// Matrix / Tone Edit). Split into a TabbedComponent because the device can't
// be in Storage Mode (needed for file transfer) and respond as a live MIDI
// device at the same time -- see PLAN.md.
//
// Owns the one shared CCTransmitter (MIDI connection) and passes it by
// reference into every tab that needs it -- MC-707's MIDI output port is
// exclusive on Windows (confirmed empirically), so only one connection to
// the device can exist at a time; tabs must share it, not each open their own.
class MainComponent : public juce::Component
{
public:
    MainComponent();

    void resized() override;

private:
    CCTransmitter transmitter;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
