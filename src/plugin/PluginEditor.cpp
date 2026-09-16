#include "PluginEditor.h"

namespace openpedal {

OpenPedalEditor::OpenPedalEditor(OpenPedalProcessor& p)
    : AudioProcessorEditor(&p), processor_(p)
{
    setSize(720, 360);
    setResizable(true, true);
}

void OpenPedalEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e22));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(18.0f));
    g.drawText("OpenPedal", getLocalBounds().removeFromTop(40), juce::Justification::centred);
}

void OpenPedalEditor::resized() {}

} // namespace openpedal
