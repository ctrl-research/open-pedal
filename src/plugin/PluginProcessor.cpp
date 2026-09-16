#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace openpedal {

OpenPedalProcessor::OpenPedalProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    chain_ = std::make_unique<Chain>();
}

OpenPedalProcessor::~OpenPedalProcessor() = default;

void OpenPedalProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate;
    blockSize_ = samplesPerBlock;
    if (chain_)
        chain_->prepare(sampleRate, samplesPerBlock);
}

void OpenPedalProcessor::releaseResources() {}

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

    // Guitar is mono: process channel 0 (summing a stereo input) and fan out to every output.
    float* mono = buffer.getWritePointer(0);
    if (numIn > 1) {
        const float* right = buffer.getReadPointer(1);
        for (int i = 0; i < numSamples; ++i)
            mono[i] = 0.5f * (mono[i] + right[i]);
    }

    if (chain_)
        chain_->process(mono, numSamples);

    for (int ch = 1; ch < numOut; ++ch)
        buffer.copyFrom(ch, 0, mono, numSamples);
}

double OpenPedalProcessor::getTailLengthSeconds() const
{
    double tail = 0.0;
    if (chain_)
        for (int i = 0; i < chain_->numSlots(); ++i)
            if (const auto& p = chain_->slot(i).pedal)
                tail += p->descriptor().tailSeconds;
    return tail;
}

juce::AudioProcessorEditor* OpenPedalProcessor::createEditor()
{
    return new OpenPedalEditor(*this);
}

void OpenPedalProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    nlohmann::json state;
    state["board"] = boardToJson(board_, false);
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
            board_ = boardFromJson(state["board"]);
    } catch (const std::exception&) {
        // Malformed state: keep the current board rather than crash the host.
    }
}

} // namespace openpedal

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new openpedal::OpenPedalProcessor();
}
