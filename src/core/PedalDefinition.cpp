#include "core/PedalDefinition.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace openpedal {

namespace {

using nlohmann::json;

struct Collector {
    std::vector<std::string>& errors;
    void add(std::string msg) { errors.push_back(std::move(msg)); }
};

bool validId(const std::string& s)
{
    if (s.empty())
        return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '-' || c == '_';
    });
}

std::optional<ParamDescriptor> parseKnob(const json& j, std::size_t index, Collector& errs)
{
    const std::string where = "knobs[" + std::to_string(index) + "]";
    if (!j.is_object()) {
        errs.add(where + " must be an object");
        return std::nullopt;
    }
    ParamDescriptor p;
    if (!j.contains("id") || !j["id"].is_string() || !validId(j["id"].get<std::string>())) {
        errs.add(where + " needs an 'id' (letters, digits, '.', '-', '_')");
        return std::nullopt;
    }
    p.id = j["id"].get<std::string>();
    const std::string me = "knob '" + p.id + "'";

    p.name = j.value("name", p.id);
    p.unit = j.value("unit", "");
    p.automatable = j.value("automatable", true);
    p.smoothingMs = j.value("smoothing_ms", 20.0);

    const std::string typeStr = j.value("type", "float");
    const auto type = parseParamType(typeStr);
    if (!type) {
        errs.add(me + " has unknown type '" + typeStr + "' (float, int, bool, enum)");
        return std::nullopt;
    }
    p.type = *type;

    const std::string taperStr = j.value("taper", "linear");
    const auto taper = parseTaper(taperStr);
    if (!taper) {
        errs.add(me + " has unknown taper '" + taperStr + "' (linear, log, audio)");
        return std::nullopt;
    }
    p.taper = *taper;

    switch (p.type) {
        case ParamType::Enum: {
            if (!j.contains("values") || !j["values"].is_array() || j["values"].size() < 2) {
                errs.add(me + " is an enum and needs a 'values' array with at least two labels");
                return std::nullopt;
            }
            for (const auto& v : j["values"]) {
                if (!v.is_string()) {
                    errs.add(me + " 'values' must all be strings");
                    return std::nullopt;
                }
                p.enumValues.push_back(v.get<std::string>());
            }
            p.min = 0;
            p.max = static_cast<double>(p.enumValues.size() - 1);
            p.taper = Taper::Linear;
            p.smoothingMs = 0;
            if (j.contains("default")) {
                const auto& d = j["default"];
                if (d.is_string()) {
                    const auto idx = p.enumIndex(d.get<std::string>());
                    if (!idx) {
                        errs.add(me + " default '" + d.get<std::string>() + "' is not one of its values");
                        return std::nullopt;
                    }
                    p.defaultValue = *idx;
                } else if (d.is_number()) {
                    p.defaultValue = d.get<double>();
                } else {
                    errs.add(me + " default must be a label or an index");
                    return std::nullopt;
                }
            }
            break;
        }
        case ParamType::Bool:
            p.min = 0;
            p.max = 1;
            p.smoothingMs = 0;
            if (j.contains("default")) {
                const auto& d = j["default"];
                if (d.is_boolean()) p.defaultValue = d.get<bool>() ? 1.0 : 0.0;
                else if (d.is_number()) p.defaultValue = d.get<double>();
                else {
                    errs.add(me + " default must be true/false");
                    return std::nullopt;
                }
            }
            break;
        case ParamType::Int:
        case ParamType::Float: {
            for (const char* key : {"min", "max"}) {
                if (!j.contains(key) || !j[key].is_number()) {
                    errs.add(me + " needs numeric '" + key + "'");
                    return std::nullopt;
                }
            }
            p.min = j["min"].get<double>();
            p.max = j["max"].get<double>();
            if (j.contains("default")) {
                if (!j["default"].is_number()) {
                    errs.add(me + " default must be a number");
                    return std::nullopt;
                }
                p.defaultValue = j["default"].get<double>();
            } else {
                p.defaultValue = p.min;
            }
            if (p.type == ParamType::Int)
                p.smoothingMs = 0;
            break;
        }
    }

    std::string err;
    if (!p.validate(err)) {
        errs.add(err);
        return std::nullopt;
    }
    return p;
}

std::optional<ParamSource> parseParamSource(const json& v, const std::string& where, Collector& errs)
{
    if (v.is_number())
        return ParamSource{v.get<double>()};
    if (v.is_boolean())
        return ParamSource{v.get<bool>() ? 1.0 : 0.0};
    if (v.is_string())
        return ParamSource{v.get<std::string>()};
    if (v.is_object()) {
        if (!v.contains("knob") || !v["knob"].is_string()) {
            errs.add(where + " binding needs a 'knob' id");
            return std::nullopt;
        }
        KnobBinding b;
        b.knobId = v["knob"].get<std::string>();
        if (v.contains("scale")) {
            if (!v["scale"].is_number()) { errs.add(where + " 'scale' must be a number"); return std::nullopt; }
            b.scale = v["scale"].get<double>();
        }
        if (v.contains("offset")) {
            if (!v["offset"].is_number()) { errs.add(where + " 'offset' must be a number"); return std::nullopt; }
            b.offset = v["offset"].get<double>();
        }
        if (v.contains("map")) {
            if (!v["map"].is_object()) { errs.add(where + " 'map' must be an object of knob value -> option"); return std::nullopt; }
            for (auto it = v["map"].begin(); it != v["map"].end(); ++it) {
                if (!it.value().is_string()) { errs.add(where + " 'map' values must be option names"); return std::nullopt; }
                b.labelMap[it.key()] = it.value().get<std::string>();
            }
        }
        return ParamSource{b};
    }
    errs.add(where + " must be a number, a label, or a {\"knob\": ...} binding");
    return std::nullopt;
}

} // namespace

PedalParseResult parsePedal(const json& j)
{
    PedalParseResult result;
    Collector errs{result.errors};

    if (!j.is_object()) {
        errs.add("pedal must be a JSON object");
        return result;
    }

    PedalDefinition def;
    def.source = j;
    auto& d = def.descriptor;

    const int schema = j.value("schema", 1);
    if (schema > kPedalSchemaVersion)
        errs.add("pedal schema version " + std::to_string(schema) + " is newer than this build supports");

    if (!j.contains("id") || !j["id"].is_string() || !validId(j["id"].get<std::string>()))
        errs.add("pedal needs an 'id' such as \"yourname.pedal-name\" (letters, digits, '.', '-', '_')");
    else
        d.id = j["id"].get<std::string>();

    d.name = j.value("name", d.id);
    d.version = j.value("version", "1.0.0");
    d.category = j.value("category", "utility");
    d.author = j.value("author", "");
    d.description = j.value("description", "");
    d.tailSeconds = j.value("tail_seconds", 0.0);
    if (d.tailSeconds < 0.0)
        errs.add("tail_seconds must be >= 0");

    d.oversampling = j.value("oversampling", 1);
    if (d.oversampling != 1 && d.oversampling != 2 && d.oversampling != 4 && d.oversampling != 8)
        errs.add("oversampling must be 1, 2, 4 or 8");

    if (j.contains("knobs")) {
        if (!j["knobs"].is_array()) {
            errs.add("'knobs' must be an array");
        } else {
            std::set<std::string> seen;
            for (std::size_t i = 0; i < j["knobs"].size(); ++i) {
                if (auto k = parseKnob(j["knobs"][i], i, errs)) {
                    if (!seen.insert(k->id).second)
                        errs.add("duplicate knob id '" + k->id + "'");
                    d.params.push_back(std::move(*k));
                }
            }
        }
    }

    if (!j.contains("nodes") || !j["nodes"].is_array()) {
        errs.add("'nodes' must be an array of DSP blocks");
    } else {
        std::set<std::string> seen;
        for (std::size_t i = 0; i < j["nodes"].size(); ++i) {
            const auto& n = j["nodes"][i];
            const std::string where = "nodes[" + std::to_string(i) + "]";
            if (!n.is_object()) { errs.add(where + " must be an object"); continue; }
            NodeDef node;
            if (!n.contains("id") || !n["id"].is_string() || !validId(n["id"].get<std::string>())) {
                errs.add(where + " needs an 'id'");
                continue;
            }
            node.id = n["id"].get<std::string>();
            if (node.id == kGraphInputId || node.id == kGraphOutputId) {
                errs.add("node id '" + node.id + "' is reserved");
                continue;
            }
            if (!seen.insert(node.id).second)
                errs.add("duplicate node id '" + node.id + "'");
            if (!n.contains("type") || !n["type"].is_string()) {
                errs.add("node '" + node.id + "' needs a 'type'");
                continue;
            }
            node.type = n["type"].get<std::string>();
            for (auto it = n.begin(); it != n.end(); ++it) {
                if (it.key() == "id" || it.key() == "type")
                    continue;
                if (auto src = parseParamSource(it.value(), "node '" + node.id + "' param '" + it.key() + "'", errs))
                    node.params[it.key()] = std::move(*src);
            }
            def.nodes.push_back(std::move(node));
        }
    }

    if (!j.contains("connections") || !j["connections"].is_array()) {
        errs.add("'connections' must be an array of [from, to] pairs");
    } else {
        for (std::size_t i = 0; i < j["connections"].size(); ++i) {
            const auto& c = j["connections"][i];
            const std::string where = "connections[" + std::to_string(i) + "]";
            if (!c.is_array() || c.size() != 2 || !c[0].is_string() || !c[1].is_string()) {
                errs.add(where + " must be [\"from\", \"to\"] with node ids as strings");
                continue;
            }
            ConnectionDef conn;
            conn.from = c[0].get<std::string>();
            const std::string to = c[1].get<std::string>();
            const auto dot = to.find('.');
            if (dot == std::string::npos) {
                conn.toNode = to;
            } else {
                conn.toNode = to.substr(0, dot);
                conn.toPort = to.substr(dot + 1);
            }
            def.connections.push_back(std::move(conn));
        }
    }

    if (result.errors.empty())
        result.definition = std::move(def);
    return result;
}

PedalParseResult parsePedalString(const std::string& text)
{
    json j;
    try {
        j = json::parse(text);
    } catch (const json::parse_error& e) {
        PedalParseResult r;
        r.errors.push_back(std::string("pedal is not valid JSON: ") + e.what());
        return r;
    }
    return parsePedal(j);
}

} // namespace openpedal
