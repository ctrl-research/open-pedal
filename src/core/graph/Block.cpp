#include "core/graph/Block.h"

#include <algorithm>

namespace openpedal::graph {

BlockRegistry& BlockRegistry::instance()
{
    static BlockRegistry registry;
    return registry;
}

void BlockRegistry::add(BlockSpec spec, Factory factory)
{
    for (auto& e : entries_)
        if (e.spec.type == spec.type) {
            e = Entry{std::move(spec), std::move(factory)};
            return;
        }
    entries_.push_back(Entry{std::move(spec), std::move(factory)});
}

const BlockSpec* BlockRegistry::find(std::string_view type) const
{
    for (const auto& e : entries_)
        if (e.spec.type == type)
            return &e.spec;
    return nullptr;
}

std::unique_ptr<Block> BlockRegistry::create(std::string_view type) const
{
    for (const auto& e : entries_)
        if (e.spec.type == type)
            return e.factory();
    return nullptr;
}

std::vector<const BlockSpec*> BlockRegistry::all() const
{
    std::vector<const BlockSpec*> out;
    out.reserve(entries_.size());
    for (const auto& e : entries_)
        out.push_back(&e.spec);
    std::sort(out.begin(), out.end(), [](const BlockSpec* a, const BlockSpec* b) { return a->type < b->type; });
    return out;
}

// Defined in each block translation unit; referencing them forces the linker to keep the
// static registrars even when the blocks live in a static library.
namespace blocks {
void linkGain();
void linkFilter();
void linkWaveshaper();
void linkDelay();
void linkLfo();
void linkUtility();
} // namespace blocks

void ensureBuiltinBlocksRegistered()
{
    static const bool once = [] {
        blocks::linkGain();
        blocks::linkFilter();
        blocks::linkWaveshaper();
        blocks::linkDelay();
        blocks::linkLfo();
        blocks::linkUtility();
        return true;
    }();
    (void) once;
}

} // namespace openpedal::graph
