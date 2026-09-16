#pragma once

#include "core/PedalDescriptor.h"

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace openpedal {

// A node parameter bound to a pedal knob: value = knob * scale + offset (numeric knobs), or a
// label-mapped enum (enum knobs bound to enum block params).
struct KnobBinding {
    std::string knobId;
    double scale = 1.0;
    double offset = 0.0;
    std::map<std::string, std::string> labelMap; // enum knobs: knob value -> block option label
    bool operator==(const KnobBinding&) const = default;
};

// A literal number, a literal enum label, or a knob binding.
using ParamSource = std::variant<double, std::string, KnobBinding>;

struct NodeDef {
    std::string id;
    std::string type;
    std::map<std::string, ParamSource> params;
};

struct ConnectionDef {
    std::string from;   // "in" or a node id
    std::string toNode; // a node id or "output"
    std::string toPort; // input port name; empty means the node's first input
};

inline constexpr int kPedalSchemaVersion = 1;
inline constexpr const char* kGraphInputId = "in";
inline constexpr const char* kGraphOutputId = "output";

// The parsed form of a pedal definition file: metadata + knobs (descriptor) and the DSP graph.
struct PedalDefinition {
    PedalDescriptor descriptor;
    std::vector<NodeDef> nodes;
    std::vector<ConnectionDef> connections;
    nlohmann::json source; // original JSON, kept for embedding into board exports
};

struct PedalParseResult {
    std::optional<PedalDefinition> definition; // set only when errors is empty
    std::vector<std::string> errors;

    bool ok() const { return definition.has_value(); }
};

// Parse and structurally validate a pedal definition. Collects every problem it can find so
// authors fix a file in one pass. Graph-level checks (unknown blocks, cycles, bad bindings)
// happen when the pedal is compiled; see graph/GraphPedal.h.
PedalParseResult parsePedal(const nlohmann::json& j);
PedalParseResult parsePedalString(const std::string& text);

} // namespace openpedal
