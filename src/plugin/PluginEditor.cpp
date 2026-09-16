#include "PluginEditor.h"

namespace openpedal {

namespace {

const juce::Colour kBackground{0xff1b1c20};
const juce::Colour kPanel{0xff2a2c33};
const juce::Colour kText{0xffe8e8ea};
const juce::Colour kMuted{0xff9a9ca6};

juce::Colour categoryColour(const std::string& category)
{
    if (category == "drive") return juce::Colour(0xffd9772b);
    if (category == "modulation") return juce::Colour(0xff4f9dd9);
    if (category == "delay") return juce::Colour(0xff6cc07a);
    if (category == "reverb") return juce::Colour(0xff8a7bd6);
    if (category == "dynamics") return juce::Colour(0xffd6c25b);
    if (category == "filter") return juce::Colour(0xffd65b9a);
    if (category == "pitch") return juce::Colour(0xff5bd6c9);
    return juce::Colour(0xff8c8f99);
}

} // namespace

// ---- PedalPanel -----------------------------------------------------------------------------------

PedalPanel::PedalPanel(OpenPedalProcessor& p, int slot) : processor_(p), slot_(slot)
{
    const Board& board = processor_.board();
    const auto& inst = board.chain[static_cast<std::size_t>(slot)];
    const auto& report = processor_.lastReport();
    const ResolutionReport::Item* item = slot < static_cast<int>(report.items.size()) ? &report.items[static_cast<std::size_t>(slot)] : nullptr;

    const PedalDescriptor* desc = nullptr;
    if (const auto* e = processor_.collection().find(inst.pedalId, inst.versionReq))
        desc = &e->definition.descriptor;
    else if (auto emb = board.embeddedPedals.find(inst.pedalId); emb != board.embeddedPedals.end())
        if (auto parsed = parsePedal(emb->second); parsed.ok())
            static_cast<void>(desc = nullptr); // embedded definitions are described via the parameters below

    resolved_ = item && (item->status == ResolutionReport::Status::Local || item->status == ResolutionReport::Status::Embedded);

    // Prefer the live parameter descriptors: they describe exactly what the chain is running.
    const ParamDescriptor* firstKnob = processor_.knobParam(slot, 0)->descriptor();
    if (resolved_ && desc) {
        title_ = juce::String(desc->name);
        subtitle_ = juce::String(desc->category) + "  v" + juce::String(desc->version);
        accent_ = categoryColour(desc->category);
    } else if (resolved_) {
        title_ = juce::String(inst.pedalId);
        subtitle_ = "embedded in board";
        accent_ = categoryColour("");
    } else {
        title_ = juce::String(inst.pedalId);
        subtitle_ = item && item->status == ResolutionReport::Status::Failed ? "failed to load" : "not installed";
        accent_ = juce::Colour(0xffb04a4a);
    }
    juce::ignoreUnused(firstKnob);

    if (item) {
        for (const auto& e : item->errors) problems_.add(juce::String(e));
        for (const auto& w : item->warnings) problems_.add(juce::String(w));
    }

    enable_.setClickingTogglesState(true);
    enable_.setColour(juce::TextButton::buttonOnColourId, accent_);
    enable_.setColour(juce::TextButton::buttonColourId, kPanel.darker(0.4f));
    // The bypass parameter is inverted relative to the "ON" button, so wire it by hand.
    enable_.setToggleState(!processor_.bypassParam(slot)->get(), juce::dontSendNotification);
    enable_.onClick = [this] { processor_.setSlotEnabled(slot_, enable_.getToggleState()); };
    addAndMakeVisible(enable_);

    left_.onClick = [this] { processor_.movePedal(slot_, slot_ - 1); };
    right_.onClick = [this] { processor_.movePedal(slot_, slot_ + 1); };
    remove_.onClick = [this] { processor_.removePedal(slot_); };
    left_.setEnabled(slot > 0);
    right_.setEnabled(slot < static_cast<int>(board.chain.size()) - 1);
    addAndMakeVisible(left_);
    addAndMakeVisible(right_);
    addAndMakeVisible(remove_);

    const bool canInstall = item && item->status == ResolutionReport::Status::Embedded;
    install_.setVisible(canInstall);
    install_.onClick = [this, id = inst.pedalId] {
        std::string error;
        if (!processor_.installEmbeddedPedal(id, error))
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Install failed", juce::String(error));
    };
    addChildComponent(install_);

    for (int k = 0; k < kMaxKnobs; ++k) {
        auto* param = processor_.knobParam(slot, k);
        const ParamDescriptor* d = param->descriptor();
        if (!d)
            break;
        KnobControl kc;
        kc.label = std::make_unique<juce::Label>(juce::String(), juce::String(d->name));
        kc.label->setJustificationType(juce::Justification::centred);
        kc.label->setColour(juce::Label::textColourId, kMuted);
        kc.label->setFont(juce::FontOptions(12.0f));
        addAndMakeVisible(*kc.label);

        if (d->type == ParamType::Enum) {
            auto combo = std::make_unique<juce::ComboBox>();
            int id = 1;
            for (const auto& v : d->enumValues)
                combo->addItem(juce::String(v), id++);
            kc.combo = std::make_unique<juce::ComboBoxParameterAttachment>(*param, *combo);
            addAndMakeVisible(*combo);
            kc.control = std::move(combo);
        } else if (d->type == ParamType::Bool) {
            auto button = std::make_unique<juce::ToggleButton>();
            kc.button = std::make_unique<juce::ButtonParameterAttachment>(*param, *button);
            addAndMakeVisible(*button);
            kc.control = std::move(button);
        } else {
            auto slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, kWidth / 2 - 16, 16);
            slider->setColour(juce::Slider::rotarySliderFillColourId, accent_);
            slider->setColour(juce::Slider::textBoxTextColourId, kText);
            slider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            kc.slider = std::make_unique<juce::SliderParameterAttachment>(*param, *slider);
            addAndMakeVisible(*slider);
            kc.control = std::move(slider);
        }
        knobs_.push_back(std::move(kc));
    }
}

void PedalPanel::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(3.0f);
    g.setColour(kPanel);
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(accent_);
    g.fillRoundedRectangle(r.removeFromTop(6.0f), 3.0f);

    auto header = getLocalBounds().reduced(8).removeFromTop(44);
    g.setColour(kText);
    g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    g.drawText(title_, header.removeFromTop(22), juce::Justification::centredLeft, true);
    g.setColour(kMuted);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText(subtitle_, header, juce::Justification::centredLeft, true);

    if (!problems_.isEmpty()) {
        auto area = getLocalBounds().reduced(8).withTrimmedTop(84).withTrimmedBottom(install_.isVisible() ? 40 : 8);
        g.setColour(juce::Colour(0xffe0a05a));
        g.setFont(juce::FontOptions(11.0f));
        g.drawFittedText(problems_.joinIntoString("\n"), area, juce::Justification::topLeft, 12);
    }
}

void PedalPanel::resized()
{
    auto r = getLocalBounds().reduced(8);
    auto top = r.removeFromTop(44);
    top.removeFromLeft(top.getWidth() - 28);
    remove_.setBounds(top.removeFromTop(22).reduced(1));

    auto controls = r.removeFromTop(28);
    enable_.setBounds(controls.removeFromLeft(56));
    controls.removeFromLeft(6);
    left_.setBounds(controls.removeFromLeft(28));
    controls.removeFromLeft(4);
    right_.setBounds(controls.removeFromLeft(28));

    if (install_.isVisible())
        install_.setBounds(r.removeFromBottom(28));

    r.removeFromTop(8);
    const int cols = 2;
    const int cellW = r.getWidth() / cols;
    const int cellH = 78;
    for (std::size_t i = 0; i < knobs_.size(); ++i) {
        const int col = static_cast<int>(i) % cols;
        const int row = static_cast<int>(i) / cols;
        juce::Rectangle<int> cell(r.getX() + col * cellW, r.getY() + row * cellH, cellW, cellH);
        auto& kc = knobs_[i];
        kc.label->setBounds(cell.removeFromTop(16));
        if (kc.slider)
            kc.control->setBounds(cell.reduced(2));
        else
            kc.control->setBounds(cell.removeFromTop(24).reduced(4, 0));
    }
}

// ---- OpenPedalEditor ---------------------------------------------------------------------------------

OpenPedalEditor::OpenPedalEditor(OpenPedalProcessor& p)
    : AudioProcessorEditor(&p), processor_(p)
{
    setSize(900, 460);
    setResizable(true, true);
    setResizeLimits(600, 360, 4000, 2000);

    boardName_.setEditable(true);
    boardName_.setColour(juce::Label::textColourId, kText);
    boardName_.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    boardName_.onTextChange = [this] { processor_.setBoardName(boardName_.getText().toStdString()); };
    addAndMakeVisible(boardName_);

    addButton_.onClick = [this] { showAddPedalMenu(); };
    importButton_.onClick = [this] { showImportMenu(); };
    exportButton_.onClick = [this] { showExportMenu(); };
    reloadButton_.onClick = [this] { processor_.reloadPedals(); };
    for (auto* b : {&addButton_, &importButton_, &exportButton_, &reloadButton_})
        addAndMakeVisible(*b);

    pedalsDirLabel_.setColour(juce::Label::textColourId, kMuted);
    pedalsDirLabel_.setFont(juce::FontOptions(11.0f));
    pedalsDirLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(pedalsDirLabel_);

    viewport_.setViewedComponent(&strip_, false);
    viewport_.setScrollBarsShown(false, true);
    addAndMakeVisible(viewport_);

    messages_.setMultiLine(true);
    messages_.setReadOnly(true);
    messages_.setScrollbarsShown(true);
    messages_.setColour(juce::TextEditor::backgroundColourId, kBackground.brighter(0.05f));
    messages_.setColour(juce::TextEditor::textColourId, kMuted);
    messages_.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    addAndMakeVisible(messages_);

    processor_.boardChanged.addChangeListener(this);
    rebuild();
}

OpenPedalEditor::~OpenPedalEditor()
{
    processor_.boardChanged.removeChangeListener(this);
}

void OpenPedalEditor::changeListenerCallback(juce::ChangeBroadcaster*)
{
    rebuild();
}

void OpenPedalEditor::rebuild()
{
    const Board& board = processor_.board();
    boardName_.setText(juce::String(board.name), juce::dontSendNotification);
    pedalsDirLabel_.setText("pedals: " + juce::String(processor_.userPedalsDir().string()), juce::dontSendNotification);

    panels_.clear();
    strip_.removeAllChildren();
    for (int i = 0; i < static_cast<int>(board.chain.size()); ++i) {
        auto panel = std::make_unique<PedalPanel>(processor_, i);
        strip_.addAndMakeVisible(*panel);
        panels_.push_back(std::move(panel));
    }
    addButton_.setEnabled(static_cast<int>(board.chain.size()) < kMaxSlots);
    refreshMessages();
    resized();
}

void OpenPedalEditor::refreshMessages()
{
    juce::StringArray lines;
    for (const auto& l : processor_.lastReport().summary())
        lines.add(juce::String(l));
    for (const auto& e : processor_.collection().loadErrors())
        for (const auto& m : e.messages)
            lines.add("pedal file " + juce::String(e.source) + ": " + juce::String(m));
    if (lines.isEmpty()) {
        const auto all = processor_.collection().all();
        lines.add(juce::String(static_cast<int>(all.size())) + " pedal(s) available. Drop .json pedal files into the pedals folder; they load automatically.");
    }
    messages_.setText(lines.joinIntoString("\n"), false);
}

void OpenPedalEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBackground);
}

void OpenPedalEditor::resized()
{
    auto r = getLocalBounds().reduced(10);
    auto top = r.removeFromTop(30);
    boardName_.setBounds(top.removeFromLeft(260));
    top.removeFromLeft(10);
    addButton_.setBounds(top.removeFromLeft(100));
    top.removeFromLeft(6);
    importButton_.setBounds(top.removeFromLeft(80));
    top.removeFromLeft(6);
    exportButton_.setBounds(top.removeFromLeft(80));
    top.removeFromLeft(6);
    reloadButton_.setBounds(top.removeFromLeft(110));
    top.removeFromLeft(10);
    pedalsDirLabel_.setBounds(top);

    r.removeFromTop(8);
    messages_.setBounds(r.removeFromBottom(70));
    r.removeFromBottom(8);
    viewport_.setBounds(r);

    const int stripWidth = juce::jmax(r.getWidth(), static_cast<int>(panels_.size()) * (PedalPanel::kWidth + 6));
    strip_.setBounds(0, 0, stripWidth, r.getHeight() - (viewport_.isHorizontalScrollBarShown() ? 10 : 0));
    int x = 0;
    for (auto& panel : panels_) {
        panel->setBounds(x, 0, PedalPanel::kWidth, strip_.getHeight());
        x += PedalPanel::kWidth + 6;
    }
}

void OpenPedalEditor::showAddPedalMenu()
{
    juce::PopupMenu menu;
    const auto all = processor_.collection().all();
    if (all.empty())
        menu.addItem(-1, "No pedals found", false);
    std::string lastCategory;
    int id = 1;
    std::vector<const PedalCollection::Entry*> byId;
    for (const auto* e : all) {
        // Only the newest version of each id is offered.
        if (!byId.empty() && byId.back()->definition.descriptor.id == e->definition.descriptor.id)
            byId.back() = e;
        else
            byId.push_back(e);
    }
    std::stable_sort(byId.begin(), byId.end(), [](const auto* a, const auto* b) {
        return a->definition.descriptor.category < b->definition.descriptor.category;
    });
    for (const auto* e : byId) {
        const auto& d = e->definition.descriptor;
        if (d.category != lastCategory) {
            menu.addSectionHeader(juce::String(d.category));
            lastCategory = d.category;
        }
        menu.addItem(id++, juce::String(d.name) + "  (" + juce::String(d.id) + ")");
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addButton_), [this, byId](int result) {
        if (result <= 0 || result > static_cast<int>(byId.size()))
            return;
        const auto& d = byId[static_cast<std::size_t>(result - 1)]->definition.descriptor;
        const auto major = juce::String(d.version).upToFirstOccurrenceOf(".", false, false);
        processor_.addPedal(d.id, (major + ".x").toStdString());
    });
}

void OpenPedalEditor::showImportMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Import board from file...");
    menu.addItem(2, "Paste board from clipboard");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(importButton_), [this](int result) {
        if (result == 2) {
            importText(juce::SystemClipboard::getTextFromClipboard(), "clipboard");
        } else if (result == 1) {
            chooser_ = std::make_unique<juce::FileChooser>("Import board", juce::File(), "*.json");
            chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this](const juce::FileChooser& fc) {
                                      const auto file = fc.getResult();
                                      if (file.existsAsFile())
                                          importText(file.loadFileAsString(), file.getFileName());
                                  });
        }
    });
}

void OpenPedalEditor::importText(const juce::String& text, const juce::String& sourceName)
{
    const auto result = processor_.importBoardJson(text.toStdString());
    if (!result.ok) {
        messages_.setText("Import from " + sourceName + " failed: " + juce::String(result.error), false);
        return;
    }
    juce::StringArray lines;
    lines.add("Imported board from " + sourceName + ".");
    for (const auto& l : result.report.summary())
        lines.add(juce::String(l));
    if (!result.embeddedNotInstalled.empty())
        lines.add("Pedals embedded in this board can be installed with the Install button on their panel.");
    messages_.setText(lines.joinIntoString("\n"), false);
}

void OpenPedalEditor::showExportMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Copy board to clipboard");
    menu.addItem(2, "Copy board to clipboard (include pedal definitions)");
    menu.addSeparator();
    menu.addItem(3, "Save board to file...");
    menu.addItem(4, "Save board to file (include pedal definitions)...");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(exportButton_), [this](int result) {
        switch (result) {
            case 1: exportText(false, true); break;
            case 2: exportText(true, true); break;
            case 3: exportText(false, false); break;
            case 4: exportText(true, false); break;
            default: break;
        }
    });
}

void OpenPedalEditor::exportText(bool embedPedals, bool toClipboard)
{
    const juce::String text(processor_.exportBoardJson(embedPedals));
    if (toClipboard) {
        juce::SystemClipboard::copyTextToClipboard(text);
        messages_.setText(juce::String("Board copied to clipboard") + (embedPedals ? " with pedal definitions." : "."), false);
        return;
    }
    const auto suggested = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                               .getChildFile(juce::File::createLegalFileName(boardName_.getText().isEmpty() ? "board" : boardName_.getText()) + ".json");
    chooser_ = std::make_unique<juce::FileChooser>("Save board", suggested, "*.json");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, text, embedPedals](const juce::FileChooser& fc) {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              if (!file.hasFileExtension("json"))
                                  file = file.withFileExtension("json");
                              const bool ok = file.replaceWithText(text);
                              messages_.setText(ok ? "Saved board to " + file.getFullPathName() + (embedPedals ? " with pedal definitions." : ".")
                                                   : "Could not write " + file.getFullPathName(), false);
                          });
}

} // namespace openpedal
