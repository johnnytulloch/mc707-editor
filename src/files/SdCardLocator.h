#pragma once

#include <juce_core/juce_core.h>

// Storage Mode helper (brief §6 /files) -- locates the MC-707's SD card once
// mounted via the device's Storage Mode.
namespace SdCardLocator
{
    // Scans all filesystem roots for a ROLAND/GROOVEBOX folder. Not filtered to
    // isOnRemovableDrive() -- Windows card readers often report the media as
    // fixed rather than removable, so that flag isn't reliable. The
    // ROLAND/GROOVEBOX folder itself is a specific enough signature to search
    // for across all drives.
    //
    // Returns <root>/ROLAND/GROOVEBOX/MIDI, or an invalid (default-constructed)
    // juce::File if no such folder was found on any drive.
    juce::File findGrooveboxMidiFolder();
}
