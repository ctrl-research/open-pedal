// open-pedal-render: render a board over a WAV file offline.
//
//   open-pedal-render <board.json> <in.wav> <out.wav> [--pedals <dir>]... [--no-user-pedals]
//   open-pedal-render --check <pedal.json>...      validate pedal files, exit 1 on any error
//   open-pedal-render --list-blocks                 print the block reference as Markdown
//
// Pedals resolve from the user pedal folder, every --pedals folder, and any definitions embedded
// in the board. Used in CI and by pedal authors to hear a pedal without launching a DAW.

#include "core/Board.h"
#include "core/BoardLoader.h"
#include "core/Paths.h"
#include "core/PedalCollection.h"
#include "core/graph/Block.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

juce::File resolve(const std::string& p)
{
    return juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(p));
}

int usage()
{
    std::fprintf(stderr,
                 "usage: open-pedal-render <board.json> <in.wav> <out.wav> [--pedals <dir>]... [--no-user-pedals]\n"
                 "       open-pedal-render --check <pedal.json>...\n"
                 "       open-pedal-render --list-blocks\n");
    return 2;
}

std::string fmt(double v)
{
    char buf[64];
    if (v == std::floor(v) && std::fabs(v) < 1e15)
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v));
    else
        std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

int listBlocks()
{
    openpedal::graph::ensureBuiltinBlocksRegistered();
    for (const auto* spec : openpedal::graph::BlockRegistry::instance().all()) {
        std::printf("### `%s`\n\n%s\n\n", spec->type.c_str(), spec->doc.c_str());
        std::string inputs;
        for (const auto& i : spec->inputs)
            inputs += (inputs.empty() ? "" : ", ") + ("`" + i + "`");
        std::printf("Inputs: %s\n\n", inputs.empty() ? "none (source block)" : inputs.c_str());
        if (spec->params.empty()) {
            std::printf("No parameters.\n\n");
            continue;
        }
        std::printf("| Parameter | Default | Range | Description |\n|---|---|---|---|\n");
        for (const auto& p : spec->params) {
            std::string range;
            if (p.isEnum()) {
                for (const auto& l : p.enumLabels)
                    range += (range.empty() ? "" : ", ") + ("`" + l + "`");
            } else {
                range = fmt(p.min) + " .. " + fmt(p.max);
            }
            const std::string def = p.isEnum() ? "`" + p.enumLabels[static_cast<std::size_t>(p.defaultValue)] + "`" : fmt(p.defaultValue);
            std::printf("| `%s` | %s | %s | %s |\n", p.name.c_str(), def.c_str(), range.c_str(), p.doc.c_str());
        }
        std::printf("\n");
    }
    return 0;
}

int checkPedals(const std::vector<std::string>& files)
{
    int failures = 0;
    for (const auto& f : files) {
        openpedal::PedalCollection c;
        const juce::File file = resolve(f);
        if (!file.existsAsFile()) {
            std::printf("%s: not found\n", f.c_str());
            ++failures;
            continue;
        }
        c.addString(file.loadFileAsString().toStdString(), "check", f);
        if (c.loadErrors().empty()) {
            const auto* e = c.all().front();
            std::printf("%s: ok (%s v%s, %d knob(s))\n", f.c_str(), e->definition.descriptor.id.c_str(),
                        e->definition.descriptor.version.c_str(), static_cast<int>(e->definition.descriptor.params.size()));
            continue;
        }
        ++failures;
        for (const auto& err : c.loadErrors())
            for (const auto& m : err.messages)
                std::printf("%s: error: %s\n", f.c_str(), m.c_str());
    }
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> positional;
    std::vector<std::string> pedalDirs;
    bool useUserPedals = true;
    if (argc >= 2 && std::string(argv[1]) == "--list-blocks")
        return listBlocks();
    if (argc >= 3 && std::string(argv[1]) == "--check")
        return checkPedals(std::vector<std::string>(argv + 2, argv + argc));
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
        if (chain->slot(i).pedal && chain->isActive(i))
            ++active;
    std::printf("rendered %d samples at %.0f Hz through %d/%d active pedal(s), latency %d samples -> %s\n",
                numSamples, reader->sampleRate, active, chain->numSlots(), chain->latencySamples(),
                positional[2].c_str());
    return report.allResolved() ? 0 : 3;
}
