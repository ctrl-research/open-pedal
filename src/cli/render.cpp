// open-pedal-render: render a board over a WAV file offline.
//
//   open-pedal-render <board.json> <in.wav> <out.wav> [--pedals <dir>]... [--no-user-pedals]
//
// Pedals resolve from the user pedal folder, every --pedals folder, and any definitions embedded
// in the board. Used in CI and by pedal authors to hear a pedal without launching a DAW.

#include "core/Board.h"
#include "core/BoardLoader.h"
#include "core/Paths.h"
#include "core/PedalCollection.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

int usage()
{
    std::fprintf(stderr,
                 "usage: open-pedal-render <board.json> <in.wav> <out.wav> [--pedals <dir>]... [--no-user-pedals]\n");
    return 2;
}

juce::File resolve(const std::string& p)
{
    return juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(p));
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> positional;
    std::vector<std::string> pedalDirs;
    bool useUserPedals = true;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--pedals") {
            if (i + 1 >= argc)
                return usage();
            pedalDirs.push_back(argv[++i]);
        } else if (a == "--no-user-pedals") {
            useUserPedals = false;
        } else if (a.rfind("--", 0) == 0) {
            return usage();
        } else {
            positional.push_back(a);
        }
    }
    if (positional.size() != 3)
        return usage();

    const juce::File boardFile = resolve(positional[0]);
    const juce::File inFile = resolve(positional[1]);
    const juce::File outFile = resolve(positional[2]);

    openpedal::Board board;
    try {
        board = openpedal::boardFromJsonString(boardFile.loadFileAsString().toStdString());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    openpedal::PedalCollection collection;
    if (useUserPedals)
        collection.loadDirectory(openpedal::defaultUserPedalsDir(), "user");
    for (const auto& dir : pedalDirs)
        collection.loadDirectory(dir, "user");
    for (const auto& err : collection.loadErrors())
        for (const auto& m : err.messages)
            std::fprintf(stderr, "pedal load error: %s: %s\n", err.source.c_str(), m.c_str());

    openpedal::ResolutionReport report;
    auto chain = openpedal::buildChain(board, collection, report);
    for (const auto& line : report.summary())
        std::fprintf(stderr, "warning: %s\n", line.c_str());

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(inFile));
    if (!reader) {
        std::fprintf(stderr, "error: cannot read %s\n", positional[1].c_str());
        return 1;
    }

    const auto numSamples = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), numSamples);
    reader->read(&buffer, 0, numSamples, 0, true, true);

    juce::AudioBuffer<float> mono(1, numSamples);
    mono.clear();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        mono.addFrom(0, 0, buffer, ch, 0, numSamples, 1.0f / static_cast<float>(buffer.getNumChannels()));

    constexpr int kBlock = 512;
    chain->prepare(reader->sampleRate, kBlock);
    for (int pos = 0; pos < numSamples; pos += kBlock)
        chain->process(mono.getWritePointer(0, pos), std::min(kBlock, numSamples - pos));

    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = outFile.createOutputStream();
    if (!stream) {
        std::fprintf(stderr, "error: cannot write %s\n", positional[2].c_str());
        return 1;
    }
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions()
                                                  .withSampleRate(reader->sampleRate)
                                                  .withNumChannels(1)
                                                  .withBitsPerSample(24));
    if (!writer) {
        std::fprintf(stderr, "error: cannot create WAV writer\n");
        return 1;
    }
    writer->writeFromAudioSampleBuffer(mono, 0, numSamples);
    writer.reset();

    int active = 0;
    for (int i = 0; i < chain->numSlots(); ++i)
        if (chain->slot(i).pedal && chain->slot(i).enabled)
            ++active;
    std::printf("rendered %d samples at %.0f Hz through %d/%d active pedal(s), latency %d samples -> %s\n",
                numSamples, reader->sampleRate, active, chain->numSlots(), chain->latencySamples(),
                positional[2].c_str());
    return report.allResolved() ? 0 : 3;
}
