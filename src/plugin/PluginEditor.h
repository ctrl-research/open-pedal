#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>
#include <vector>

namespace openpedal {

juce::Colour groupColour(int group);

// Toggle button whose right-click opens a menu instead of toggling.
class GroupButton : public juce::TextButton {
public:
    std::function<void()> onRightClick;
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!e.mods.isPopupMenu())
            juce::TextButton::mouseDown(e);
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) {
            if (onRightClick)
                onRightClick();
            return;
        }
        juce::TextButton::mouseUp(e);
    }
};

// One pedal on the board: header, footswitch, knobs generated from the pedal's descriptor,
// group selector, and move/remove controls. Drag the header to reorder. Unresolved slots show
// why and, if possible, an Install button.
class PedalPanel : public juce::Component {
public:
    PedalPanel(OpenPedalProcessor& p, int slot);
    ~PedalPanel() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

    int slot() const { return slot_; }
    static constexpr int kWidth = 190;
    static constexpr int kGap = 6;

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

    int group_ = -1;
    bool groupOff_ = false;
    bool dragArmed_ = false;

    juce::TextButton enable_{"ON"};
    juce::TextButton left_{"<"}, right_{">"}, remove_{"x"}, install_{"Install pedal"};
    juce::ComboBox groupBox_;
    std::vector<KnobControl> knobs_;

    void confirmRemove();
};

// Holds the pedal panels and accepts panel drops to reorder the chain.
class PedalStrip : public juce::Component, public juce::DragAndDropTarget {
public:
    explicit PedalStrip(OpenPedalProcessor& p) : processor_(p) {}
    void paint(juce::Graphics&) override;

    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override;
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

    int numPanels = 0;

private:
    int dropIndexFor(int x) const;
    OpenPedalProcessor& processor_;
    int dropIndex_ = -1;
};

class OpenPedalEditor : public juce::AudioProcessorEditor,
                        public juce::DragAndDropContainer,
                        private juce::ChangeListener {
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

    void showGroupMenu(int group);
    void renameGroup(int group);

    juce::Viewport viewport_;
    PedalStrip strip_;
    std::vector<std::unique_ptr<PedalPanel>> panels_;
    std::vector<std::unique_ptr<GroupButton>> groupButtons_;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> groupAttachments_;
    std::unique_ptr<juce::AlertWindow> renameWindow_;

    juce::TextEditor messages_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenPedalEditor)
};

} // namespace openpedal
