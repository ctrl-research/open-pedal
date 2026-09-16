#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace openpedal {

class OpenPedalEditor : public juce::AudioProcessorEditor {
public:
    explicit OpenPedalEditor(OpenPedalProcessor&);
    ~OpenPedalEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    [[maybe_unused]] OpenPedalProcessor& processor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenPedalEditor)
};

} // namespace openpedal
