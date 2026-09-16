#pragma once

#include "core/Pedal.h"
#include "core/PedalDefinition.h"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openpedal {

// Does `version` (e.g. "1.2.3") satisfy `requirement`? Supported requirements:
//   "*" or ""      any version
//   "1.x", "1.*"   same major
//   "1.2.x"        same major and minor
//   "1.2.3"        exact
bool versionSatisfies(const std::string& version, const std::string& requirement);

// Compare two semver strings; returns <0, 0, >0.
int compareVersions(const std::string& a, const std::string& b);

// Every pedal definition the plugin knows about: bundled examples, the user's pedal folder,
// and definitions embedded in an imported board. Owns parsed definitions; compiles a fresh
// IPedal instance on request so each board slot gets its own state.
class PedalCollection {
public:
    struct Entry {
        PedalDefinition definition;
        std::filesystem::path sourceFile; // empty for bundled or embedded definitions
        std::string origin;               // "bundled", "user", "embedded"
    };
    struct LoadError {
        std::string source; // file path or pedal id
        std::vector<std::string> messages;
    };

    // Parse and add a definition. Returns false and records a LoadError on failure.
    // A later add with the same id and version replaces the earlier one.
    bool add(const nlohmann::json& pedalJson, std::string origin, std::filesystem::path sourceFile = {});
    bool addString(const std::string& text, std::string origin, std::filesystem::path sourceFile = {});

    // Load every *.json in a directory (non-recursive). Missing directory is not an error.
    void loadDirectory(const std::filesystem::path& dir, std::string origin = "user");

    // Drop entries whose origin matches (e.g. before a reload of the user folder).
    void removeOrigin(const std::string& origin);

    // Highest version satisfying the requirement, or nullptr.
    const Entry* find(const std::string& id, const std::string& versionReq = "*") const;

    // Compile a new pedal instance. On failure returns nullptr and fills `errors`.
    std::unique_ptr<IPedal> instantiate(const std::string& id, const std::string& versionReq,
                                        std::vector<std::string>& errors) const;

    std::vector<const Entry*> all() const; // sorted by id, then version
    const std::vector<LoadError>& loadErrors() const { return loadErrors_; }
    void clearLoadErrors() { loadErrors_.clear(); }

private:
    std::vector<Entry> entries_;
    std::vector<LoadError> loadErrors_;
};

} // namespace openpedal
