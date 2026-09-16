#include "core/PedalCollection.h"

#include "core/graph/GraphPedal.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace openpedal {

namespace {

std::vector<std::string> splitDots(const std::string& s)
{
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, '.'))
        parts.push_back(item);
    return parts;
}

int toInt(const std::string& s)
{
    try {
        return std::stoi(s);
    } catch (...) {
        return 0;
    }
}

} // namespace

int compareVersions(const std::string& a, const std::string& b)
{
    const auto pa = splitDots(a), pb = splitDots(b);
    for (std::size_t i = 0; i < 3; ++i) {
        const int x = i < pa.size() ? toInt(pa[i]) : 0;
        const int y = i < pb.size() ? toInt(pb[i]) : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

bool versionSatisfies(const std::string& version, const std::string& requirement)
{
    if (requirement.empty() || requirement == "*")
        return true;
    const auto v = splitDots(version), r = splitDots(requirement);
    for (std::size_t i = 0; i < r.size(); ++i) {
        if (r[i] == "x" || r[i] == "*")
            return true;
        if (i >= v.size() || toInt(v[i]) != toInt(r[i]))
            return false;
    }
    return r.size() >= 3 || v.size() <= r.size();
}

bool PedalCollection::add(const nlohmann::json& pedalJson, std::string origin, std::filesystem::path sourceFile)
{
    auto parsed = parsePedal(pedalJson);
    if (!parsed.ok()) {
        loadErrors_.push_back(LoadError{sourceFile.empty() ? pedalJson.value("id", std::string("<unknown>")) : sourceFile.string(),
                                        std::move(parsed.errors)});
        return false;
    }

    // Compile once up front so authors see graph errors at load time, not when a board uses it.
    std::vector<std::string> compileErrors;
    if (!graph::GraphPedal::compile(*parsed.definition, compileErrors)) {
        loadErrors_.push_back(LoadError{sourceFile.empty() ? parsed.definition->descriptor.id : sourceFile.string(),
                                        std::move(compileErrors)});
        return false;
    }

    const auto& d = parsed.definition->descriptor;
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&](const Entry& e) {
                                      return e.definition.descriptor.id == d.id && e.definition.descriptor.version == d.version;
                                  }),
                   entries_.end());
    entries_.push_back(Entry{std::move(*parsed.definition), std::move(sourceFile), std::move(origin)});
    return true;
}

bool PedalCollection::addString(const std::string& text, std::string origin, std::filesystem::path sourceFile)
{
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& e) {
        loadErrors_.push_back(LoadError{sourceFile.empty() ? "<string>" : sourceFile.string(),
                                        {std::string("not valid JSON: ") + e.what()}});
        return false;
    }
    return add(j, std::move(origin), std::move(sourceFile));
}

void PedalCollection::loadDirectory(const std::filesystem::path& dir, std::string origin)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
        return;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
        if (entry.is_regular_file() && entry.path().extension() == ".json")
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        std::ifstream in(f);
        std::stringstream buf;
        buf << in.rdbuf();
        addString(buf.str(), origin, f);
    }
}

void PedalCollection::removeOrigin(const std::string& origin)
{
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.origin == origin; }),
                   entries_.end());
}

const PedalCollection::Entry* PedalCollection::find(const std::string& id, const std::string& versionReq) const
{
    const Entry* best = nullptr;
    for (const auto& e : entries_) {
        if (e.definition.descriptor.id != id || !versionSatisfies(e.definition.descriptor.version, versionReq))
            continue;
        if (!best || compareVersions(e.definition.descriptor.version, best->definition.descriptor.version) > 0)
            best = &e;
    }
    return best;
}

std::unique_ptr<IPedal> PedalCollection::instantiate(const std::string& id, const std::string& versionReq,
                                                     std::vector<std::string>& errors) const
{
    const Entry* e = find(id, versionReq);
    if (!e) {
        errors.push_back("pedal '" + id + "' (version " + versionReq + ") is not installed");
        return nullptr;
    }
    return graph::GraphPedal::compile(e->definition, errors);
}

std::vector<const PedalCollection::Entry*> PedalCollection::all() const
{
    std::vector<const Entry*> out;
    for (const auto& e : entries_)
        out.push_back(&e);
    std::sort(out.begin(), out.end(), [](const Entry* a, const Entry* b) {
        if (a->definition.descriptor.id != b->definition.descriptor.id)
            return a->definition.descriptor.id < b->definition.descriptor.id;
        return compareVersions(a->definition.descriptor.version, b->definition.descriptor.version) < 0;
    });
    return out;
}

} // namespace openpedal
