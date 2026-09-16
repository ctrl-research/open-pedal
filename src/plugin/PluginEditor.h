#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>
#include <vector>

namespace openpedal {

// One pedal on the board: header, footswitch, knobs generated from the pedal's descriptor,
// and move/remove controls. Unresolved slots show why and, if possible, an Install button.
class PedalPanel : public juce::Component {
public:
    PedalPanel(OpenPedalProcessor& p, int slot);
    ~PedalPanel() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = 190;

private:
    struct KnobControl {
        std::unique_ptr<juce::Component> control;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::ParameterAttachment> attachment;        // unused; kept for symmetry
        std::unique_ptr<juce::SliderParameterAttachment> slider;
        std::unique_ptr<juce::ComboBoxParameterAttachment> combo;
        std::unique_ptr<juce::ButtonParameterAttachment> button;
    };

    OpenPedalProcessor& processor_;
    const int slot_;
    juce::String title_;
    juce::String subtitle_;
    juce::Colour accent_;
    juce::StringArray problems_;
    bool resolved_ = false;

    juce::TextButton enable_{"ON"};
    std::unique_ptr<juce::ButtonParameterAttachment> enableAttachment_;
    juce::TextButton left_{"<"}, right_{">"}, remove_{"x"}, install_{"Install pedal"};
    std::vector<KnobControl> knobs_;
};

class OpenPedalEditor : public juce::AudioProcessorEditor, private juce::ChangeListener {
public:
    explicit OpenPedalEditor(OpenPedalProcessor&);
    ~OpenPedalEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void rebuild();
    void refreshMessages();
    void showAddPedalMenu();
    void showImportMenu();
    void showExportMenu();
    void importText(const juce::String& text, const juce::String& sourceName);
    void exportText(bool embedPedals, bool toClipboard);

    OpenPedalProcessor& processor_;

    juce::Label boardName_;
    juce::TextButton addButton_{"+ Add pedal"};
    juce::TextButton importButton_{"Import"};
    juce::TextButton exportButton_{"Export"};
    juce::TextButton reloadButton_{"Reload pedals"};
    juce::Label pedalsDirLabel_;

    juce::Viewport viewport_;
    juce::Component strip_;
    std::vector<std::unique_ptr<PedalPanel>> panels_;

    juce::TextEditor messages_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenPedalEditor)
};

} // namespace openpedal
