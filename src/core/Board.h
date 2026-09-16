#pragma once

#include <nlohmann/json.hpp>

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace openpedal {

inline constexpr int kBoardSchemaVersion = 1;

// One pedal placed on a board. Parameter values are kept as JSON so the model stays faithful
// to the file: floats, ints, bools, and enum labels all round-trip without a descriptor.
struct PedalInstance {
    std::string pedalId;                          // "author.pedal-name"
    std::string versionReq = "*";                 // semver requirement such as "1.x" or "*"
    bool enabled = true;
    std::map<std::string, nlohmann::json> params; // knob id -> real-unit value or enum label

    bool operator==(const PedalInstance&) const = default;
};

// A pedalboard: an ordered chain of pedal instances plus metadata.
// `embeddedPedals` carries full pedal definitions when a board was exported with
// "include pedal definitions" so recipients without those pedals can still load it.
struct Board {
    int schema = kBoardSchemaVersion;
    std::string name;
    std::string author;
    double inputGainDb = 0.0;
    std::vector<PedalInstance> chain;
    std::map<std::string, nlohmann::json> embeddedPedals; // pedal id -> pedal definition JSON

    bool operator==(const Board&) const = default;
};

struct BoardError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Parse a board from JSON. Throws BoardError with a user-readable message on malformed input.
// Unknown top-level keys are ignored so newer files load in older builds where possible.
Board boardFromJson(const nlohmann::json& j);
Board boardFromJsonString(const std::string& text);

// Serialise a board. `includeEmbeddedPedals` controls whether the "pedals" object is written.
nlohmann::json boardToJson(const Board& board, bool includeEmbeddedPedals);
std::string boardToJsonString(const Board& board, bool includeEmbeddedPedals, int indent = 2);

} // namespace openpedal
