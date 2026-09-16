#pragma once

#include "core/Board.h"
#include "core/Chain.h"
#include "core/PedalCollection.h"

#include <memory>
#include <string>
#include <vector>

namespace openpedal {

// How each chain slot of a board was resolved when building a Chain.
struct ResolutionReport {
    enum class Status {
        Local,    // found in the pedal collection
        Embedded, // not installed, but the board carried the definition
        Missing,  // neither installed nor embedded: slot is a bypass placeholder
        Failed,   // definition found but did not compile: slot is a bypass placeholder
    };
    struct Item {
        int slot = 0;
        std::string pedalId;
        std::string versionReq;
        Status status = Status::Missing;
        std::vector<std::string> errors;   // compile errors for Failed, load errors for Embedded
        std::vector<std::string> warnings; // unknown knob ids, bad enum labels, ...
    };
    std::vector<Item> items;

    bool allResolved() const;
    std::vector<std::string> summary() const; // one human-readable line per problem
};

// Apply a board instance's stored parameter values to a pedal, converting enum labels to
// indices. Unknown knobs and unknown labels are reported as warnings, never errors.
void applyInstanceParams(IPedal& pedal, const PedalInstance& inst, std::vector<std::string>& warnings);

// Build a prepared-later Chain for a board. Pedals resolve from the collection first, then from
// the board's embedded definitions. Never fails: unresolvable slots become bypass placeholders.
// Call chain->prepare() before processing.
std::unique_ptr<Chain> buildChain(const Board& board, const PedalCollection& collection, ResolutionReport& report);

} // namespace openpedal
