#pragma once

#include "core/Pedal.h"
#include "core/PedalDefinition.h"
#include "core/graph/Block.h"

#include <juce_dsp/juce_dsp.h>

#include <memory>
#include <string>
#include <vector>

namespace openpedal::graph {

// IPedal backed by a JSON-defined graph of blocks.
//
// Signal flow per sample: the pedal input feeds "in"; nodes run in topological order; every
// input port sums the current-sample outputs of its sources; "output" sums its sources. Edges
// that close a loop into a `delay` node read the previous sample instead, giving one-sample
// delayed feedback. The whole graph runs at the oversampled rate when oversampling > 1.
//
// Knob values are smoothed and pushed into bound block parameters every kControlInterval samples.
class GraphPedal final : public IPedal {
public:
    static constexpr int kControlInterval = 16;

    // Build a pedal from a parsed definition. On failure returns nullptr and fills `errors`
    // with every graph problem found (unknown block, bad param, cycle without delay, ...).
    static std::unique_ptr<GraphPedal> compile(const PedalDefinition& def, std::vector<std::string>& errors);

    const PedalDescriptor& descriptor() const override { return descriptor_; }
    const PedalDefinition& definition() const { return definition_; }

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setParam(std::string_view paramId, double realValue) override;
    double getParam(std::string_view paramId) const override;
    void process(float* buffer, int numSamples) override;
    int latencySamples() const override;

private:
    GraphPedal() = default;

    struct Node {
        std::unique_ptr<Block> block;
        std::string id;
        int numInputs = 0;
        int portOffset = 0; // into portSourceBegin_
    };
    struct Binding {
        int knob = -1;
        int node = -1;
        int param = -1;
        double scale = 1.0;
        double offset = 0.0;
        std::vector<int> enumMap; // knob enum index -> block enum index; empty if numeric
    };
    struct Literal {
        int node = -1;
        int param = -1;
        double value = 0.0;
    };
    struct Knob {
        double target = 0.0;
        double current = 0.0;
        double step = 0.0;
        int stepsRemaining = 0;
        bool dirty = true;
    };

    void controlTick(bool force);
    void pushBinding(const Binding& b, double knobValue);
    float tickGraph(float input);
    void processAtRate(float* buffer, int numSamples);

    PedalDescriptor descriptor_;
    PedalDefinition definition_;

    std::vector<Node> nodes_;                 // topological order
    std::vector<int> portSourceBegin_;        // per (node, port): start into sources_; one extra sentinel per node
    std::vector<int> sources_;                // node index, or -1 for the pedal input
    std::vector<int> outputSources_;
    std::vector<float> outputs_;              // last output of every node
    std::vector<float> inputScratch_;

    std::vector<Binding> bindings_;
    std::vector<Literal> literals_;
    std::vector<Knob> knobs_;                 // parallel to descriptor_.params
    std::vector<double> knobStepsPerMs_;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler_;
    double baseRate_ = 48000.0;
    double processRate_ = 48000.0;
    int controlCounter_ = 0;
    bool prepared_ = false;
};

} // namespace openpedal::graph
