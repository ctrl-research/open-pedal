#pragma once

#include "SlotParameter.h"
#include "core/Board.h"
#include "core/BoardLoader.h"
#include "core/Chain.h"
#include "core/PedalCollection.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace openpedal {

inline constexpr int kMaxSlots = 8;
inline constexpr int kMaxKnobs = 8;

// The plugin. Owns the board (message-thread model), the pedal collection, the host-visible
// parameters, and the active Chain (audio thread). All board edits go through the methods
// below, which rebuild the chain off the audio thread and swap it in atomically.
class OpenPedalProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    OpenPedalProcessor();
    ~OpenPedalProcessor() override;

    // ---- juce::AudioProcessor ------------------------------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ---- Board model (message thread) ----------------------------------------------------------
    // The board reflects the latest parameter values whenever it is read through here.
    const Board& board();
    const ResolutionReport& lastReport() const { return report_; }
    const PedalCollection& collection() const { return collection_; }
    std::filesystem::path userPedalsDir() const { return userPedalsDir_; }

    void setBoardName(const std::string& name);
    void setBoardAuthor(const std::string& author);
    void setInputGainDb(double db);
    bool addPedal(const std::string& pedalId, const std::string& versionReq = "*"); // false if the board is full
    void removePedal(int slot);
    void movePedal(int from, int to);
    void setSlotEnabled(int slot, bool enabled);
    void clearBoard();

    // Replace the whole board (import). Missing pedals become bypass placeholders; see lastReport().
    void setBoard(Board newBoard);

    // Export the current board as JSON text. With `embedPedals`, every resolvable pedal's
    // definition is included so recipients can load it without installing anything.
    std::string exportBoardJson(bool embedPedals);

    struct ImportResult {
        bool ok = false;
        std::string error;     // set when the JSON could not be parsed at all
        ResolutionReport report;
        std::vector<std::string> embeddedNotInstalled; // pedal ids the user could install
    };
    ImportResult importBoardJson(const std::string& text);

    // Write an embedded pedal definition from the current board into the user pedal folder.
    bool installEmbeddedPedal(const std::string& pedalId, std::string& error);

    // Re-scan the user pedal folder and rebuild the chain. Also happens automatically when the
    // folder changes on disk.
    void reloadPedals();

    // ---- Parameters -----------------------------------------------------------------------------
    SlotParameter* knobParam(int slot, int knob) const { return knobParams_[static_cast<std::size_t>(slot * kMaxKnobs + knob)]; }
    SlotBypassParameter* bypassParam(int slot) const { return bypassParams_[static_cast<std::size_t>(slot)]; }

    // Fires on the message thread after any board or pedal-collection change.
    juce::ChangeBroadcaster boardChanged;

private:
    void timerCallback() override;

    void loadBundledPedals();
    void seedUserPedalsDir();
    void scanUserPedalsDir(bool force);
    void rebuildChain();
    void syncParamsFromBoard();
    void syncBoardFromParams();
    void pruneGraveyard();
    void notifyBoardChanged();

    Board board_;
    PedalCollection collection_;
    ResolutionReport report_;
    std::filesystem::path userPedalsDir_;
    std::filesystem::file_time_type userDirStamp_{};
    std::uintmax_t userDirSignature_ = 0;

    std::vector<SlotParameter*> knobParams_;        // owned by AudioProcessor
    std::vector<SlotBypassParameter*> bypassParams_; // owned by AudioProcessor

    std::unique_ptr<Chain> chain_;                   // owner; only touched on the message thread
    std::atomic<Chain*> activeChain_{nullptr};       // read by the audio thread
    std::vector<std::pair<std::unique_ptr<Chain>, juce::int64>> graveyard_;

    double sampleRate_ = 44100.0;
    int blockSize_ = 512;
    std::atomic<bool> prepared_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenPedalProcessor)
};

} // namespace openpedal
