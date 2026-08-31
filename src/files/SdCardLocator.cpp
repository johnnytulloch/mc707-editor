#include "SdCardLocator.h"

juce::File SdCardLocator::findGrooveboxMidiFolder()
{
    juce::Array<juce::File> roots;
    juce::File::findFileSystemRoots (roots);

    for (auto& root : roots)
    {
        auto groovebox = root.getChildFile ("ROLAND").getChildFile ("GROOVEBOX");
        if (groovebox.isDirectory())
            return groovebox.getChildFile ("MIDI");
    }

    return {};
}
