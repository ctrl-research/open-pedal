#include "PluginProcessor.h"

#include "BundledPedals.h"
#include "PluginEditor.h"
#include "core/Paths.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <sstream>

namespace openpedal {

namespace {

constexpr int kGraveyardMs = 1000;

std::uintmax_t directorySignature(const std::filesystem::path& dir, std::filesystem::file_time_type& newest)
{
    std::error_code ec;
    std::uintmax_t sig = 0;
    newest = {};
    if (!std::filesystem::is_directory(dir, ec))
        return 0;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".json")
            continue;
        const auto t = e.last_write_time(ec);
        if (t > newest)
            newest = t;
        sig = sig * 1000003u + std::hash<std::string>{}(e.path().filename().string()) + static_cast<std::uintmax_t>(e.file_size(ec));
    }
    return sig;
}

} // namespace

OpenPedalProcessor::OpenPedalProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    for (int g = 0; g < kMaxGroups; ++g) {
        auto* gp = new GroupParameter(g);
        groupParams_.push_back(gp);
        addParameter(gp);
    }
    for (int s = 0; s < kMaxSlots; ++s) {
        auto* bypass = new SlotBypassParameter(s);
        bypassParams_.push_back(bypass);
        addParameter(bypass);
        for (int k = 0; k < kMaxKnobs; ++k) {
            auto* p = new SlotParameter(s, k);
            knobParams_.push_back(p);
            addParameter(p);
        }
    }

    userPedalsDir_ = defaultUserPedalsDir();
    loadBundledPedals();
    seedUserPedalsDir();
    scanUserPedalsDir(true);

    board_.name = "New Board";
    ensureGroups();
    rebuildChain();
    syncParamsFromBoard();
    startTimer(500);
}

OpenPedalProcessor::~OpenPedalProcessor()
{
    stopTimer();
    activeChain_.store(nullptr);
}

// ---- Pedal collection ---------------------------------------------------------------------------

void OpenPedalProcessor::loadBundledPedals()
{
    for (int i = 0; i < BundledPedals::namedResourceListSize; ++i) {
        int size = 0;
        const char* data = BundledPedals::getNamedResource(BundledPedals::namedResourceList[i], size);
        if (data && size > 0)
            collection_.addString(std::string(data, static_cast<std::size_t>(size)), "bundled");
    }
}

void OpenPedalProcessor::seedUserPedalsDir()
{
    std::error_code ec;
    if (std::filesystem::exists(userPedalsDir_, ec))
        return;
    if (!std::filesystem::create_directories(userPedalsDir_, ec))
        return;
    for (int i = 0; i < BundledPedals::namedResourceListSize; ++i) {
        int size = 0;
        const char* data = BundledPedals::getNamedResource(BundledPedals::namedResourceList[i], size);
        const char* original = BundledPedals::getNamedResourceOriginalFilename(BundledPedals::namedResourceList[i]);
        if (!data || !original)
            continue;
        std::ofstream out(userPedalsDir_ / original, std::ios::binary);
        out.write(data, size);
    }
}

void OpenPedalProcessor::scanUserPedalsDir(bool force)
{
    std::filesystem::file_time_type newest;
    const auto sig = directorySignature(userPedalsDir_, newest);
    if (!force && sig == userDirSignature_ && newest == userDirStamp_)
        return;
    userDirSignature_ = sig;
    userDirStamp_ = newest;
    collection_.clearLoadErrors();
    collection_.removeOrigin("user");
    collection_.loadDirectory(userPedalsDir_, "user");
}

void OpenPedalProcessor::reloadPedals()
{
    syncBoardFromParams();
    scanUserPedalsDir(true);
    rebuildChain();
    syncParamsFromBoard();
    notifyBoardChanged();
}

void OpenPedalProcessor::timerCallback()
{
    pruneGraveyard();
    std::filesystem::file_time_type newest;
    if (directorySignature(userPedalsDir_, newest) != userDirSignature_ || newest != userDirStamp_)
        reloadPedals();
}

// ---- Chain lifecycle ----------------------------------------------------------------------------

void OpenPedalProcessor::rebuildChain()
{
    report_ = ResolutionReport{};
    auto fresh = buildChain(board_, collection_, report_);
    if (prepared_.load())
        fresh->prepare(sampleRate_, blockSize_);

    Chain* raw = fresh.get();
    if (chain_)
        graveyard_.emplace_back(std::move(chain_), juce::Time::currentTimeMillis());
    chain_ = std::move(fresh);
    activeChain_.store(raw, std::memory_order_release);

    setLatencySamples(raw->latencySamples());
}

void OpenPedalProcessor::pruneGraveyard()
{
    const auto now = juce::Time::currentTimeMillis();
    graveyard_.erase(std::remove_if(graveyard_.begin(), graveyard_.end(),
                                    [&](const auto& e) { return now - e.second > kGraveyardMs; }),
                     graveyard_.end());
}

void OpenPedalProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate;
    blockSize_ = samplesPerBlock;
    if (chain_) {
        chain_->prepare(sampleRate, samplesPerBlock);
        setLatencySamples(chain_->latencySamples());
    }
    for (auto* p : knobParams_)
        p->setValue(p->getValue()); // mark every knob dirty so the fresh chain gets current values
    prepared_.store(true);
}

void OpenPedalProcessor::releaseResources()
{
    prepared_.store(false);
}

bool OpenPedalProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

void OpenPedalProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    if (numSamples == 0 || numOut == 0)
        return;

    Chain* chain = activeChain_.load(std::memory_order_acquire);

    // Push parameter changes into the chain. Values are converted through the descriptor of
    // the pedal currently in that slot; slots without a pedal ignore their parameters.
    if (chain) {
        for (int g = 0; g < kMaxGroups; ++g)
            if (groupParam(g)->consumeChange())
                chain->setGroupEnabled(g, groupParam(g)->get());
        for (int s = 0; s < kMaxSlots && s < chain->numSlots(); ++s) {
            if (bypassParam(s)->consumeChange())
                chain->setEnabled(s, !bypassParam(s)->get());
            IPedal* pedal = chain->slot(s).pedal.get();
            if (!pedal)
                continue;
            const auto& params = pedal->descriptor().params;
            for (int k = 0; k < kMaxKnobs && k < static_cast<int>(params.size()); ++k) {
                auto* p = knobParam(s, k);
                if (p->consumeChange())
                    pedal->setParam(params[static_cast<std::size_t>(k)].id, params[static_cast<std::size_t>(k)].toReal(p->getValue()));
            }
        }
    }

    // Guitar is mono: process channel 0 (summing a stereo input) and fan out to every output.
    float* mono = buffer.getWritePointer(0);
    if (numIn > 1) {
        const float* right = buffer.getReadPointer(1);
        for (int i = 0; i < numSamples; ++i)
            mono[i] = 0.5f * (mono[i] + right[i]);
    }

    if (chain)
        chain->process(mono, numSamples);

    for (int ch = 1; ch < numOut; ++ch)
        buffer.copyFrom(ch, 0, mono, numSamples);
}

double OpenPedalProcessor::getTailLengthSeconds() const
{
    double tail = 0.0;
    if (Chain* chain = activeChain_.load(std::memory_order_acquire))
        for (int i = 0; i < chain->numSlots(); ++i)
            if (const auto& p = chain->slot(i).pedal)
                tail += p->descriptor().tailSeconds;
    return tail;
}

// ---- Parameter <-> board sync -------------------------------------------------------------------

void OpenPedalProcessor::ensureGroups()
{
    for (int g = static_cast<int>(board_.groups.size()); g < kMaxGroups; ++g)
        board_.groups.push_back(PedalGroup{.id = "g" + std::to_string(g + 1), .name = "Group " + std::to_string(g + 1)});
    if (static_cast<int>(board_.groups.size()) > kMaxGroups)
        board_.groups.resize(static_cast<std::size_t>(kMaxGroups));
}

void OpenPedalProcessor::syncParamsFromBoard()
{
    for (int g = 0; g < kMaxGroups && g < static_cast<int>(board_.groups.size()); ++g) {
        groupParam(g)->setGroupName(juce::String(board_.groups[static_cast<std::size_t>(g)].name));
        groupParam(g)->setValueNotifyingHost(board_.groups[static_cast<std::size_t>(g)].enabled ? 1.0f : 0.0f);
    }
    for (int s = 0; s < kMaxSlots; ++s) {
        const bool hasSlot = chain_ && s < chain_->numSlots();
        IPedal* pedal = hasSlot ? chain_->slot(s).pedal.get() : nullptr;
        const juce::String pedalName = pedal ? juce::String(pedal->descriptor().name) : juce::String();

        bypassParam(s)->setPedalName(pedalName);
        const bool enabled = hasSlot ? chain_->slot(s).enabled : true;
        bypassParam(s)->setValueNotifyingHost(enabled ? 0.0f : 1.0f);

        for (int k = 0; k < kMaxKnobs; ++k) {
            auto* p = knobParam(s, k);
            if (pedal && k < static_cast<int>(pedal->descriptor().params.size())) {
                const auto& d = pedal->descriptor().params[static_cast<std::size_t>(k)];
                p->setDescriptor(&d, pedalName);
                p->setValueNotifyingHost(static_cast<float>(d.toNormalised(pedal->getParam(d.id))));
            } else {
                p->setDescriptor(nullptr, {});
                p->setValueNotifyingHost(0.0f);
            }
        }
    }
    updateHostDisplay(ChangeDetails().withParameterInfoChanged(true));
}

void OpenPedalProcessor::syncBoardFromParams()
{
    if (!chain_)
        return;
    for (int g = 0; g < kMaxGroups && g < static_cast<int>(board_.groups.size()); ++g)
        board_.groups[static_cast<std::size_t>(g)].enabled = groupParam(g)->get();
    for (int s = 0; s < kMaxSlots && s < static_cast<int>(board_.chain.size()); ++s) {
        auto& inst = board_.chain[static_cast<std::size_t>(s)];
        inst.enabled = !bypassParam(s)->get();
        IPedal* pedal = chain_->slot(s).pedal.get();
        if (!pedal)
            continue; // keep whatever the file said for unresolved pedals
        const auto& params = pedal->descriptor().params;
        for (int k = 0; k < kMaxKnobs && k < static_cast<int>(params.size()); ++k) {
            const auto& d = params[static_cast<std::size_t>(k)];
            const double real = d.toReal(knobParam(s, k)->getValue());
            if (d.type == ParamType::Enum) {
                if (auto label = d.enumLabel(real))
                    inst.params[d.id] = std::string(*label);
            } else if (d.type == ParamType::Bool) {
                inst.params[d.id] = real >= 0.5;
            } else if (d.type == ParamType::Int) {
                inst.params[d.id] = static_cast<int>(std::lround(real));
            } else {
                inst.params[d.id] = real;
            }
        }
    }
}

void OpenPedalProcessor::notifyBoardChanged()
{
    boardChanged.sendChangeMessage();
}

// ---- Board editing ------------------------------------------------------------------------------

const Board& OpenPedalProcessor::board()
{
    syncBoardFromParams();
    return board_;
}

void OpenPedalProcessor::setBoardName(const std::string& name) { board_.name = name; notifyBoardChanged(); }
void OpenPedalProcessor::setBoardAuthor(const std::string& author) { board_.author = author; notifyBoardChanged(); }

void OpenPedalProcessor::setInputGainDb(double db)
{
    board_.inputGainDb = db;
    if (chain_)
        chain_->setInputGainDb(db); // atomic float write; safe to do live
    notifyBoardChanged();
}

bool OpenPedalProcessor::addPedal(const std::string& pedalId, const std::string& versionReq)
{
    if (static_cast<int>(board_.chain.size()) >= kMaxSlots)
        return false;
    syncBoardFromParams();
    PedalInstance inst;
    inst.pedalId = pedalId;
    inst.versionReq = versionReq;
    if (const auto* e = collection_.find(pedalId, versionReq))
        for (const auto& p : e->definition.descriptor.params)
            inst.params[p.id] = p.type == ParamType::Enum ? nlohmann::json(p.enumValues[static_cast<std::size_t>(p.defaultValue)])
                                                          : nlohmann::json(p.defaultValue);
    board_.chain.push_back(std::move(inst));
    rebuildChain();
    syncParamsFromBoard();
    notifyBoardChanged();
    return true;
}

void OpenPedalProcessor::removePedal(int slot)
{
    if (slot < 0 || slot >= static_cast<int>(board_.chain.size()))
        return;
    syncBoardFromParams();
    board_.chain.erase(board_.chain.begin() + slot);
    rebuildChain();
    syncParamsFromBoard();
    notifyBoardChanged();
}

void OpenPedalProcessor::movePedal(int from, int to)
{
    const int n = static_cast<int>(board_.chain.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return;
    syncBoardFromParams();
    auto inst = std::move(board_.chain[static_cast<std::size_t>(from)]);
    board_.chain.erase(board_.chain.begin() + from);
    board_.chain.insert(board_.chain.begin() + to, std::move(inst));
    rebuildChain();
    syncParamsFromBoard();
    notifyBoardChanged();
}

void OpenPedalProcessor::setSlotEnabled(int slot, bool enabled)
{
    if (slot < 0 || slot >= static_cast<int>(board_.chain.size()))
        return;
    bypassParam(slot)->setValueNotifyingHost(enabled ? 0.0f : 1.0f);
    board_.chain[static_cast<std::size_t>(slot)].enabled = enabled;
    notifyBoardChanged();
}

void OpenPedalProcessor::clearBoard()
{
    board_.chain.clear();
    board_.embeddedPedals.clear();
    rebuildChain();
    syncParamsFromBoard();
    notifyBoardChanged();
}

void OpenPedalProcessor::setBoard(Board newBoard)
{
    if (static_cast<int>(newBoard.chain.size()) > kMaxSlots)
        newBoard.chain.resize(static_cast<std::size_t>(kMaxSlots));
    board_ = std::move(newBoard);
    ensureGroups();
    rebuildChain();
    syncParamsFromBoard();
    notifyBoardChanged();
}

void OpenPedalProcessor::setPedalGroup(int slot, int group)
{
    if (slot < 0 || slot >= static_cast<int>(board_.chain.size()))
        return;
    syncBoardFromParams();
    ensureGroups();
    board_.chain[static_cast<std::size_t>(slot)].group =
        (group >= 0 && group < kMaxGroups) ? board_.groups[static_cast<std::size_t>(group)].id : std::string();
    rebuildChain();
    syncParamsFromBoard();
    notifyBoardChanged();
}

void OpenPedalProcessor::setGroupEnabled(int group, bool enabled)
{
    if (group < 0 || group >= kMaxGroups)
        return;
    groupParam(group)->setValueNotifyingHost(enabled ? 1.0f : 0.0f);
    ensureGroups();
    board_.groups[static_cast<std::size_t>(group)].enabled = enabled;
    notifyBoardChanged();
}

void OpenPedalProcessor::setGroupName(int group, const std::string& name)
{
    if (group < 0 || group >= kMaxGroups)
        return;
    ensureGroups();
    board_.groups[static_cast<std::size_t>(group)].name = name.empty() ? "Group " + std::to_string(group + 1) : name;
    groupParam(group)->setGroupName(juce::String(board_.groups[static_cast<std::size_t>(group)].name));
    updateHostDisplay(ChangeDetails().withParameterInfoChanged(true));
    notifyBoardChanged();
}

bool OpenPedalProcessor::isSlotActive(int slot) const
{
    if (slot < 0 || slot >= static_cast<int>(board_.chain.size()))
        return false;
    const auto& inst = board_.chain[static_cast<std::size_t>(slot)];
    if (bypassParam(slot)->get())
        return false;
    const int g = inst.group.empty() ? -1 : board_.groupIndex(inst.group);
    return g < 0 || groupParam(g)->get();
}

std::string OpenPedalProcessor::exportBoardJson(bool embedPedals)
{
    syncBoardFromParams();
    Board out = board_;
    // Drop groups nobody uses and nobody renamed so simple boards stay simple.
    {
        bool anyUsed = false;
        for (const auto& inst : out.chain)
            anyUsed = anyUsed || !inst.group.empty();
        bool anyRenamed = false;
        for (std::size_t g = 0; g < out.groups.size(); ++g)
            anyRenamed = anyRenamed || out.groups[g].name != "Group " + std::to_string(g + 1) || !out.groups[g].enabled;
        if (!anyUsed && !anyRenamed)
            out.groups.clear();
    }
    if (embedPedals) {
        for (const auto& inst : out.chain) {
            if (const auto* e = collection_.find(inst.pedalId, inst.versionReq))
                out.embeddedPedals[inst.pedalId] = e->definition.source;
            // otherwise keep whatever embedded definition the board already carried
        }
    } else {
        out.embeddedPedals.clear();
    }
    return boardToJsonString(out, embedPedals);
}

OpenPedalProcessor::ImportResult OpenPedalProcessor::importBoardJson(const std::string& text)
{
    ImportResult result;
    Board incoming;
    try {
        incoming = boardFromJsonString(text);
    } catch (const BoardError& e) {
        result.error = e.what();
        return result;
    }
    if (static_cast<int>(incoming.chain.size()) > kMaxSlots)
        incoming.chain.resize(static_cast<std::size_t>(kMaxSlots));

    setBoard(std::move(incoming));
    result.ok = true;
    result.report = report_;
    for (const auto& item : report_.items)
        if (item.status == ResolutionReport::Status::Embedded)
            result.embeddedNotInstalled.push_back(item.pedalId);
    return result;
}

bool OpenPedalProcessor::installEmbeddedPedal(const std::string& pedalId, std::string& error)
{
    const auto it = board_.embeddedPedals.find(pedalId);
    if (it == board_.embeddedPedals.end()) {
        error = "the board does not embed a definition for '" + pedalId + "'";
        return false;
    }
    auto parsed = parsePedal(it->second);
    if (!parsed.ok()) {
        error = parsed.errors.empty() ? "invalid pedal definition" : parsed.errors.front();
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(userPedalsDir_, ec);
    std::string file = pedalId;
    for (auto& c : file)
        if (c == '.' || c == '/' || c == '\\')
            c = '-';
    const auto path = userPedalsDir_ / (file + ".json");
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "could not write " + path.string();
        return false;
    }
    out << it->second.dump(2) << '\n';
    out.close();
    reloadPedals();
    return true;
}

// ---- State ----------------------------------------------------------------------------------------

void OpenPedalProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    syncBoardFromParams();
    nlohmann::json state;
    state["board"] = boardToJson(board_, true);
    state["ui"] = nlohmann::json::object();
    const std::string text = state.dump();
    destData.replaceAll(text.data(), text.size());
}

void OpenPedalProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;
    try {
        const auto state = nlohmann::json::parse(static_cast<const char*>(data),
                                                 static_cast<const char*>(data) + sizeInBytes);
        if (state.contains("board"))
            setBoard(boardFromJson(state["board"]));
    } catch (const std::exception&) {
        // Malformed state: keep the current board rather than crash the host.
    }
}

juce::AudioProcessorEditor* OpenPedalProcessor::createEditor()
{
    return new OpenPedalEditor(*this);
}

} // namespace openpedal

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new openpedal::OpenPedalProcessor();
}
