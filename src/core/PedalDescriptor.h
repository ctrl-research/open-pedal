#pragma once

#include "core/ParamDescriptor.h"

#include <string>
#include <string_view>
#include <vector>

namespace openpedal {

// Static, sound-independent facts about a pedal definition: identity, metadata,
// processing requirements, and the knobs it exposes.
struct PedalDescriptor {
    std::string id;          // stable, never renamed, e.g. "openpedal.ts-drive"
    std::string name;        // display name
    std::string version;     // semver "1.0.0"
    std::string category;    // drive, modulation, delay, reverb, dynamics, filter, pitch, utility
    std::string author;
    std::string description;

    int oversampling = 1;    // 1, 2, 4 or 8
    double tailSeconds = 0;  // how long the pedal keeps producing sound after input stops

    std::vector<ParamDescriptor> params;

    const ParamDescriptor* findParam(std::string_view paramId) const
    {
        for (const auto& p : params)
            if (p.id == paramId)
                return &p;
        return nullptr;
    }
};

} // namespace openpedal
