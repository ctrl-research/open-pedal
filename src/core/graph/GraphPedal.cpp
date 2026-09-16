#include "core/graph/GraphPedal.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace openpedal::graph {

namespace {

struct Edge {
    int from; // -1 = pedal input
    int toNode;
    int toPort;
    bool feedback = false;
};

} // namespace

std::unique_ptr<GraphPedal> GraphPedal::compile(const PedalDefinition& def, std::vector<std::string>& errors)
{
    ensureBuiltinBlocksRegistered();
    const auto& registry = BlockRegistry::instance();
    const std::size_t errorsBefore = errors.size();

    std::unique_ptr<GraphPedal> pedal(new GraphPedal());
    pedal->descriptor_ = def.descriptor;
    pedal->definition_ = def;

    // --- Nodes and blocks -------------------------------------------------------------------
    std::map<std::string, int> nodeIndex;
    std::vector<const BlockSpec*> specs;
    for (const auto& n : def.nodes) {
        const BlockSpec* spec = registry.find(n.type);
        if (!spec) {
            errors.push_back("node '" + n.id + "' has unknown block type '" + n.type + "'");
            continue;
        }
        nodeIndex[n.id] = static_cast<int>(pedal->nodes_.size());
        Node node;
        node.block = registry.create(n.type);
        node.id = n.id;
        node.numInputs = static_cast<int>(spec->inputs.size());
        pedal->nodes_.push_back(std::move(node));
        specs.push_back(spec);
    }
    if (errors.size() != errorsBefore)
        return nullptr;

    // --- Parameters: literals and knob bindings ---------------------------------------------
    const auto knobIndex = [&](const std::string& id) -> int {
        for (std::size_t i = 0; i < def.descriptor.params.size(); ++i)
            if (def.descriptor.params[i].id == id)
                return static_cast<int>(i);
        return -1;
    };

    for (std::size_t ni = 0; ni < def.nodes.size(); ++ni) {
        const auto& n = def.nodes[ni];
        const auto it = nodeIndex.find(n.id);
        if (it == nodeIndex.end())
            continue;
        const int node = it->second;
        const BlockSpec& spec = *specs[static_cast<std::size_t>(node)];

        for (const auto& [pname, source] : n.params) {
            const std::string where = "node '" + n.id + "' param '" + pname + "'";
            const int pi = spec.paramIndex(pname);
            if (pi < 0) {
                std::string known;
                for (const auto& p : spec.params)
                    known += (known.empty() ? "" : ", ") + p.name;
                errors.push_back(where + " does not exist on block '" + spec.type + "' (has: " + known + ")");
                continue;
            }
            const ParamSpec& ps = spec.params[static_cast<std::size_t>(pi)];

            if (const auto* num = std::get_if<double>(&source)) {
                if (*num < ps.min || *num > ps.max) {
                    errors.push_back(where + " value " + std::to_string(*num) + " is outside " + std::to_string(ps.min)
                                     + ".." + std::to_string(ps.max));
                    continue;
                }
                pedal->literals_.push_back(Literal{node, pi, *num});
            } else if (const auto* label = std::get_if<std::string>(&source)) {
                if (!ps.isEnum()) {
                    errors.push_back(where + " is numeric but was given the text \"" + *label + "\"");
                    continue;
                }
                const auto found = std::find(ps.enumLabels.begin(), ps.enumLabels.end(), *label);
                if (found == ps.enumLabels.end()) {
                    std::string opts;
                    for (const auto& l : ps.enumLabels)
                        opts += (opts.empty() ? "" : ", ") + l;
                    errors.push_back(where + " has unknown option \"" + *label + "\" (options: " + opts + ")");
                    continue;
                }
                pedal->literals_.push_back(
                    Literal{node, pi, static_cast<double>(std::distance(ps.enumLabels.begin(), found))});
            } else if (const auto* bind = std::get_if<KnobBinding>(&source)) {
                const int ki = knobIndex(bind->knobId);
                if (ki < 0) {
                    errors.push_back(where + " is bound to unknown knob '" + bind->knobId + "'");
                    continue;
                }
                const ParamDescriptor& knob = def.descriptor.params[static_cast<std::size_t>(ki)];
                Binding b;
                b.knob = ki;
                b.node = node;
                b.param = pi;
                b.scale = bind->scale;
                b.offset = bind->offset;
                if (ps.isEnum()) {
                    if (knob.type != ParamType::Enum) {
                        errors.push_back(where + " is an option list, so knob '" + knob.id + "' must be an enum knob");
                        continue;
                    }
                    bool bad = false;
                    for (const auto& v : knob.enumValues) {
                        const auto mapped = bind->labelMap.find(v);
                        const std::string& target = mapped == bind->labelMap.end() ? v : mapped->second;
                        const auto lit = std::find(ps.enumLabels.begin(), ps.enumLabels.end(), target);
                        if (lit == ps.enumLabels.end()) {
                            std::string opts;
                            for (const auto& l : ps.enumLabels)
                                opts += (opts.empty() ? "" : ", ") + l;
                            errors.push_back(where + ": knob '" + knob.id + "' value \"" + v + "\" maps to \"" + target
                                             + "\", which is not an option of block '" + spec.type + "' (options: " + opts + ")");
                            bad = true;
                            break;
                        }
                        b.enumMap.push_back(static_cast<int>(std::distance(ps.enumLabels.begin(), lit)));
                    }
                    if (bad)
                        continue;
                } else if (knob.type == ParamType::Enum) {
                    errors.push_back(where + " is numeric but knob '" + knob.id + "' is an enum");
                    continue;
                }
                pedal->bindings_.push_back(std::move(b));
            }
        }
    }

    // --- Connections --------------------------------------------------------------------------
    std::vector<Edge> edges;
    for (const auto& c : def.connections) {
        const std::string where = "connection " + c.from + " -> " + c.toNode + (c.toPort.empty() ? "" : "." + c.toPort);

        int from = -1;
        if (c.from != kGraphInputId) {
            const auto it = nodeIndex.find(c.from);
            if (it == nodeIndex.end()) {
                errors.push_back(where + ": unknown source '" + c.from + "'");
                continue;
            }
            from = it->second;
        }
        if (c.toNode == kGraphInputId) {
            errors.push_back(where + ": nothing can feed into 'in'");
            continue;
        }
        if (c.toNode == kGraphOutputId) {
            if (!c.toPort.empty())
                errors.push_back(where + ": 'output' has no ports");
            else
                pedal->outputSources_.push_back(from);
            continue;
        }
        const auto it = nodeIndex.find(c.toNode);
        if (it == nodeIndex.end()) {
            errors.push_back(where + ": unknown target '" + c.toNode + "'");
            continue;
        }
        const BlockSpec& spec = *specs[static_cast<std::size_t>(it->second)];
        int port = 0;
        if (spec.inputs.empty()) {
            errors.push_back(where + ": block '" + spec.type + "' has no inputs");
            continue;
        }
        if (!c.toPort.empty()) {
            port = spec.inputIndex(c.toPort);
            if (port < 0) {
                std::string ports;
                for (const auto& p : spec.inputs)
                    ports += (ports.empty() ? "" : ", ") + p;
                errors.push_back(where + ": block '" + spec.type + "' has no input '" + c.toPort + "' (has: " + ports + ")");
                continue;
            }
        }
        edges.push_back(Edge{from, it->second, port});
    }
    if (pedal->outputSources_.empty())
        errors.push_back("nothing is connected to 'output'; the pedal would be silent");
    if (errors.size() != errorsBefore)
        return nullptr;

    // --- Feedback detection and topological order -------------------------------------------
    const int N = static_cast<int>(pedal->nodes_.size());
    std::vector<std::vector<int>> adj(static_cast<std::size_t>(N));
    for (const auto& e : edges)
        if (e.from >= 0)
            adj[static_cast<std::size_t>(e.from)].push_back(e.toNode);

    const auto reachable = [&](int start, int target) {
        std::vector<bool> seen(static_cast<std::size_t>(N), false);
        std::vector<int> stack{start};
        while (!stack.empty()) {
            const int n = stack.back();
            stack.pop_back();
            if (n == target)
                return true;
            if (seen[static_cast<std::size_t>(n)])
                continue;
            seen[static_cast<std::size_t>(n)] = true;
            for (int m : adj[static_cast<std::size_t>(n)])
                stack.push_back(m);
        }
        return false;
    };

    for (auto& e : edges) {
        if (e.from < 0)
            continue;
        const bool intoDelay = specs[static_cast<std::size_t>(e.toNode)]->type == "delay";
        if (intoDelay && reachable(e.toNode, e.from))
            e.feedback = true;
    }

    std::vector<int> indegree(static_cast<std::size_t>(N), 0);
    std::vector<std::vector<int>> fwd(static_cast<std::size_t>(N));
    for (const auto& e : edges) {
        if (e.from < 0 || e.feedback)
            continue;
        fwd[static_cast<std::size_t>(e.from)].push_back(e.toNode);
        indegree[static_cast<std::size_t>(e.toNode)]++;
    }
    std::vector<int> order;
    std::vector<int> ready;
    for (int i = 0; i < N; ++i)
        if (indegree[static_cast<std::size_t>(i)] == 0)
            ready.push_back(i);
    while (!ready.empty()) {
        const int n = ready.front();
        ready.erase(ready.begin());
        order.push_back(n);
        for (int m : fwd[static_cast<std::size_t>(n)])
            if (--indegree[static_cast<std::size_t>(m)] == 0)
                ready.push_back(m);
    }
    if (static_cast<int>(order.size()) != N) {
        std::string stuck;
        for (int i = 0; i < N; ++i)
            if (indegree[static_cast<std::size_t>(i)] > 0)
                stuck += (stuck.empty() ? "" : ", ") + pedal->nodes_[static_cast<std::size_t>(i)].id;
        errors.push_back("the graph has a feedback loop that does not pass through a 'delay' node (involving: " + stuck + ")");
        return nullptr;
    }

    // --- Reorder nodes topologically and build flat port tables ----------------------------------
    std::vector<int> newIndex(static_cast<std::size_t>(N));
    for (int pos = 0; pos < N; ++pos)
        newIndex[static_cast<std::size_t>(order[static_cast<std::size_t>(pos)])] = pos;

    std::vector<Node> reordered;
    reordered.reserve(static_cast<std::size_t>(N));
    for (int old : order)
        reordered.push_back(std::move(pedal->nodes_[static_cast<std::size_t>(old)]));
    pedal->nodes_ = std::move(reordered);

    const auto remap = [&](int idx) { return idx < 0 ? -1 : newIndex[static_cast<std::size_t>(idx)]; };
    for (auto& b : pedal->bindings_) b.node = remap(b.node);
    for (auto& l : pedal->literals_) l.node = remap(l.node);
    for (auto& s : pedal->outputSources_) s = remap(s);
    for (auto& e : edges) { e.from = remap(e.from); e.toNode = remap(e.toNode); }

    pedal->portSourceBegin_.clear();
    pedal->sources_.clear();
    for (int n = 0; n < N; ++n) {
        auto& node = pedal->nodes_[static_cast<std::size_t>(n)];
        node.portOffset = static_cast<int>(pedal->portSourceBegin_.size());
        for (int p = 0; p < node.numInputs; ++p) {
            pedal->portSourceBegin_.push_back(static_cast<int>(pedal->sources_.size()));
            for (const auto& e : edges)
                if (e.toNode == n && e.toPort == p)
                    pedal->sources_.push_back(e.from);
        }
        pedal->portSourceBegin_.push_back(static_cast<int>(pedal->sources_.size())); // sentinel
    }

    pedal->outputs_.assign(static_cast<std::size_t>(N), 0.0f);
    int maxInputs = 1;
    for (const auto& n : pedal->nodes_)
        maxInputs = std::max(maxInputs, n.numInputs);
    pedal->inputScratch_.assign(static_cast<std::size_t>(maxInputs), 0.0f);

    pedal->knobs_.resize(def.descriptor.params.size());
    for (std::size_t i = 0; i < def.descriptor.params.size(); ++i) {
        pedal->knobs_[i].target = pedal->knobs_[i].current = def.descriptor.params[i].defaultValue;
        pedal->knobs_[i].dirty = true;
    }
    return pedal;
}

void GraphPedal::prepare(double sampleRate, int maxBlockSize)
{
    baseRate_ = sampleRate;
    const int factor = std::max(1, descriptor_.oversampling);
    if (factor > 1) {
        const auto stages = static_cast<std::size_t>(std::lround(std::log2(factor)));
        oversampler_ = std::make_unique<juce::dsp::Oversampling<float>>(
            1, stages, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        oversampler_->initProcessing(static_cast<std::size_t>(std::max(1, maxBlockSize)));
    } else {
        oversampler_.reset();
    }
    processRate_ = sampleRate * factor;

    for (auto& n : nodes_)
        n.block->prepare(processRate_);
    for (const auto& l : literals_)
        nodes_[static_cast<std::size_t>(l.node)].block->setParam(l.param, l.value);

    knobStepsPerMs_.assign(knobs_.size(), processRate_ / 1000.0 / kControlInterval);
    for (auto& k : knobs_) {
        k.current = k.target;
        k.stepsRemaining = 0;
        k.dirty = true;
    }
    prepared_ = true;
    controlCounter_ = 0;
    controlTick(true);
    reset();
}

void GraphPedal::reset()
{
    for (auto& n : nodes_)
        n.block->reset();
    std::fill(outputs_.begin(), outputs_.end(), 0.0f);
    if (oversampler_)
        oversampler_->reset();
    for (auto& k : knobs_) {
        k.current = k.target;
        k.stepsRemaining = 0;
        k.dirty = true;
    }
    controlCounter_ = 0;
    if (prepared_)
        controlTick(true);
}

void GraphPedal::setParam(std::string_view paramId, double realValue)
{
    for (std::size_t i = 0; i < descriptor_.params.size(); ++i) {
        const auto& p = descriptor_.params[i];
        if (p.id != paramId)
            continue;
        auto& k = knobs_[i];
        k.target = p.clamp(realValue);
        if (!prepared_ || p.isDiscrete() || p.smoothingMs <= 0.0) {
            k.current = k.target;
            k.stepsRemaining = 0;
        } else {
            const int steps = std::max(1, static_cast<int>(std::lround(p.smoothingMs * knobStepsPerMs_[i])));
            k.stepsRemaining = steps;
            k.step = (k.target - k.current) / steps;
        }
        k.dirty = true;
        return;
    }
}

double GraphPedal::getParam(std::string_view paramId) const
{
    for (std::size_t i = 0; i < descriptor_.params.size(); ++i)
        if (descriptor_.params[i].id == paramId)
            return knobs_[i].target;
    return 0.0;
}

void GraphPedal::pushBinding(const Binding& b, double knobValue)
{
    Block& block = *nodes_[static_cast<std::size_t>(b.node)].block;
    const ParamSpec& ps = block.spec().params[static_cast<std::size_t>(b.param)];
    double v;
    if (!b.enumMap.empty()) {
        const auto idx = static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(knobValue)), 0,
                                                             static_cast<int>(b.enumMap.size()) - 1));
        v = b.enumMap[idx];
    } else {
        v = knobValue * b.scale + b.offset;
    }
    block.setParam(b.param, std::clamp(v, ps.min, ps.max));
}

void GraphPedal::controlTick(bool force)
{
    for (auto& k : knobs_) {
        if (k.stepsRemaining > 0) {
            k.current += k.step;
            if (--k.stepsRemaining == 0)
                k.current = k.target;
            k.dirty = true;
        }
    }
    for (const auto& b : bindings_) {
        auto& k = knobs_[static_cast<std::size_t>(b.knob)];
        if (force || k.dirty)
            pushBinding(b, k.current);
    }
    for (auto& k : knobs_)
        k.dirty = false;
}

float GraphPedal::tickGraph(float input)
{
    if (++controlCounter_ >= kControlInterval) {
        controlCounter_ = 0;
        controlTick(false);
    }

    const int N = static_cast<int>(nodes_.size());
    for (int n = 0; n < N; ++n) {
        Node& node = nodes_[static_cast<std::size_t>(n)];
        for (int p = 0; p < node.numInputs; ++p) {
            const int begin = portSourceBegin_[static_cast<std::size_t>(node.portOffset + p)];
            const int end = portSourceBegin_[static_cast<std::size_t>(node.portOffset + p + 1)];
            float sum = 0.0f;
            for (int s = begin; s < end; ++s) {
                const int src = sources_[static_cast<std::size_t>(s)];
                sum += src < 0 ? input : outputs_[static_cast<std::size_t>(src)];
            }
            inputScratch_[static_cast<std::size_t>(p)] = sum;
        }
        outputs_[static_cast<std::size_t>(n)] = node.block->tick(inputScratch_.data());
    }

    float out = 0.0f;
    for (int src : outputSources_)
        out += src < 0 ? input : outputs_[static_cast<std::size_t>(src)];
    return out;
}

void GraphPedal::processAtRate(float* buffer, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
        buffer[i] = tickGraph(buffer[i]);
}

void GraphPedal::process(float* buffer, int numSamples)
{
    if (!prepared_ || numSamples <= 0)
        return;
    if (!oversampler_) {
        processAtRate(buffer, numSamples);
        return;
    }
    juce::dsp::AudioBlock<float> block(&buffer, 1, static_cast<std::size_t>(numSamples));
    auto up = oversampler_->processSamplesUp(block);
    processAtRate(up.getChannelPointer(0), static_cast<int>(up.getNumSamples()));
    oversampler_->processSamplesDown(block);
}

int GraphPedal::latencySamples() const
{
    return oversampler_ ? static_cast<int>(std::lround(oversampler_->getLatencyInSamples())) : 0;
}

} // namespace openpedal::graph
