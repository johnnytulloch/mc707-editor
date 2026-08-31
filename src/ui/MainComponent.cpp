#include "MainComponent.h"
#include "ControlPanelComponent.h"
#include "FileTransferComponent.h"
#include "ClipMatrixComponent.h"

MainComponent::MainComponent()
{
    auto tabBackground = juce::Colour (0xff0d0d0d);
    tabs.addTab ("Control Panel", tabBackground, new ControlPanelComponent(), true);
    tabs.addTab ("File Transfer", tabBackground, new FileTransferComponent(), true);
    tabs.addTab ("Clip Matrix", tabBackground, new ClipMatrixComponent(), true);
    addAndMakeVisible (tabs);

    setSize (1500, 900);
}

void MainComponent::resized()
{
    tabs.setBounds (getLocalBounds());
}
