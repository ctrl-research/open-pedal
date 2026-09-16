#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace openpedal::graph {

// Static description of one block parameter. Values are real units (Hz, ms, dB, ratio).
// Enum parameters take an index into `enumLabels`.
struct ParamSpec {
    std::string name;
    double defaultValue = 0.0;
    double min = -1e9;
    double max = 1e9;
    std::vector<std::string> enumLabels; // non-empty => enum parameter
    std::string doc;

    bool isEnum() const { return !enumLabels.empty(); }
};

// Static description of a block type: what inputs it has and which parameters it accepts.
// Used for validation, error messages, and generating the block reference documentation.
struct BlockSpec {
    std::string type;
    std::string doc;
    std::vector<std::string> inputs; // port names; empty for pure sources (e.g. lfo)
    std::vector<ParamSpec> params;

    int paramIndex(std::string_view name) const
    {
        for (std::size_t i = 0; i < params.size(); ++i)
            if (params[i].name == name)
                return static_cast<int>(i);
        return -1;
    }
    int inputIndex(std::string_view name) const
    {
        for (std::size_t i = 0; i < inputs.size(); ++i)
            if (inputs[i] == name)
                return static_cast<int>(i);
        return -1;
    }
};

// One DSP building block, evaluated per sample.
//
// prepare/reset/setParam run at control rate (setParam is called at most every few samples,
// never per sample), tick runs per sample with one float per declared input port.
// tick() must not allocate.
class Block {
public:
    virtual ~Block() = default;

    virtual const BlockSpec& spec() const = 0;

    virtual void prepare(double sampleRate) = 0;
    virtual void reset() = 0;

    // paramIndex refers to spec().params. Values are already clamped to the spec range.
    virtual void setParam(int paramIndex, double value) = 0;

    virtual float tick(const float* inputs) = 0;
};

// Registry of block types. Blocks register themselves at static-init time.
class BlockRegistry {
public:
    using Factory = std::function<std::unique_ptr<Block>()>;

    static BlockRegistry& instance();

    void add(BlockSpec spec, Factory factory);

    const BlockSpec* find(std::string_view type) const;
    std::unique_ptr<Block> create(std::string_view type) const;
    std::vector<const BlockSpec*> all() const; // sorted by type name

private:
    struct Entry {
        BlockSpec spec;
        Factory factory;
    };
    std::vector<Entry> entries_;
};

// Helper for block implementations: registers `T` under `spec` at static-init time.
template <typename T>
struct BlockRegistrar {
    explicit BlockRegistrar(BlockSpec spec)
    {
        BlockRegistry::instance().add(std::move(spec), [] { return std::make_unique<T>(); });
    }
};

// Called once to make sure every built-in block's translation unit is linked in.
void ensureBuiltinBlocksRegistered();

} // namespace openpedal::graph
