#include "core/BoardLoader.h"

#include "core/graph/GraphPedal.h"

namespace openpedal {

bool ResolutionReport::allResolved() const
{
    for (const auto& i : items)
        if (i.status == Status::Missing || i.status == Status::Failed || !i.warnings.empty())
            return false;
    return true;
}

std::vector<std::string> ResolutionReport::summary() const
{
    std::vector<std::string> out;
    for (const auto& i : items) {
        const std::string where = "slot " + std::to_string(i.slot + 1) + " (" + i.pedalId + ")";
        switch (i.status) {
            case Status::Local:
                break;
            case Status::Embedded:
                out.push_back(where + ": not installed, using the definition embedded in the board");
                break;
            case Status::Missing:
                out.push_back(where + ": pedal is not installed and the board does not include it; slot is bypassed");
                break;
            case Status::Failed:
                out.push_back(where + ": pedal failed to load; slot is bypassed");
                break;
        }
        for (const auto& e : i.errors)
            out.push_back(where + ": " + e);
        for (const auto& w : i.warnings)
            out.push_back(where + ": " + w);
    }
    return out;
}

void applyInstanceParams(IPedal& pedal, const PedalInstance& inst, std::vector<std::string>& warnings)
{
    const auto& desc = pedal.descriptor();
    for (const auto& [id, value] : inst.params) {
        const ParamDescriptor* p = desc.findParam(id);
        if (!p) {
            warnings.push_back("knob '" + id + "' does not exist on this pedal; ignored");
            continue;
        }
        if (value.is_string()) {
            const auto idx = p->enumIndex(value.get<std::string>());
            if (!idx) {
                warnings.push_back("knob '" + id + "' has no option \"" + value.get<std::string>() + "\"; using default");
                continue;
            }
            pedal.setParam(id, static_cast<double>(*idx));
        } else if (value.is_boolean()) {
            pedal.setParam(id, value.get<bool>() ? 1.0 : 0.0);
        } else if (value.is_number()) {
            pedal.setParam(id, value.get<double>());
        }
    }
}

std::unique_ptr<Chain> buildChain(const Board& board, const PedalCollection& collection, ResolutionReport& report)
{
    auto chain = std::make_unique<Chain>();
    chain->setInputGainDb(board.inputGainDb);
    for (std::size_t g = 0; g < board.groups.size() && g < static_cast<std::size_t>(Chain::kMaxGroups); ++g)
        chain->setGroupEnabled(static_cast<int>(g), board.groups[g].enabled);

    for (std::size_t i = 0; i < board.chain.size(); ++i) {
        const auto& inst = board.chain[i];
        ResolutionReport::Item item;
        item.slot = static_cast<int>(i);
        item.pedalId = inst.pedalId;
        item.versionReq = inst.versionReq;

        std::unique_ptr<IPedal> pedal;
        if (collection.find(inst.pedalId, inst.versionReq)) {
            pedal = collection.instantiate(inst.pedalId, inst.versionReq, item.errors);
            item.status = pedal ? ResolutionReport::Status::Local : ResolutionReport::Status::Failed;
        } else if (const auto emb = board.embeddedPedals.find(inst.pedalId); emb != board.embeddedPedals.end()) {
            auto parsed = parsePedal(emb->second);
            if (parsed.ok())
                pedal = graph::GraphPedal::compile(*parsed.definition, item.errors);
            else
                item.errors = std::move(parsed.errors);
            item.status = pedal ? ResolutionReport::Status::Embedded : ResolutionReport::Status::Failed;
        } else {
            item.status = ResolutionReport::Status::Missing;
        }

        if (pedal)
            applyInstanceParams(*pedal, inst, item.warnings);

        const int group = inst.group.empty() ? -1 : board.groupIndex(inst.group);
        chain->addSlot(std::move(pedal), inst.pedalId, inst.enabled, group);
        report.items.push_back(std::move(item));
    }
    return chain;
}

} // namespace openpedal
