#include "core/Board.h"

namespace openpedal {

namespace {

using nlohmann::json;

[[noreturn]] void fail(const std::string& msg)
{
    throw BoardError(msg);
}

template <typename T>
T getOr(const json& j, const char* key, T fallback)
{
    if (!j.contains(key) || j[key].is_null())
        return fallback;
    try {
        return j[key].get<T>();
    } catch (const json::exception&) {
        fail(std::string("board field '") + key + "' has the wrong type");
    }
}

PedalInstance instanceFromJson(const json& j, std::size_t index)
{
    const std::string where = "chain[" + std::to_string(index) + "]";
    if (!j.is_object())
        fail(where + " must be an object");
    if (!j.contains("pedal") || !j["pedal"].is_string() || j["pedal"].get<std::string>().empty())
        fail(where + " is missing a 'pedal' id");

    PedalInstance inst;
    inst.pedalId = j["pedal"].get<std::string>();
    inst.versionReq = getOr<std::string>(j, "version", "*");
    inst.enabled = getOr<bool>(j, "enabled", true);

    if (j.contains("params")) {
        const auto& params = j["params"];
        if (!params.is_object())
            fail(where + ".params must be an object");
        for (auto it = params.begin(); it != params.end(); ++it) {
            const auto& v = it.value();
            if (!(v.is_number() || v.is_string() || v.is_boolean()))
                fail(where + ".params." + it.key() + " must be a number, string, or boolean");
            inst.params[it.key()] = v;
        }
    }
    return inst;
}

} // namespace

Board boardFromJson(const json& j)
{
    if (!j.is_object())
        fail("board must be a JSON object");

    Board b;
    b.schema = getOr<int>(j, "schema", 1);
    if (b.schema < 1)
        fail("board schema version must be >= 1");
    if (b.schema > kBoardSchemaVersion)
        fail("board schema version " + std::to_string(b.schema) + " is newer than this build supports ("
             + std::to_string(kBoardSchemaVersion) + ")");

    b.name = getOr<std::string>(j, "name", "");
    b.author = getOr<std::string>(j, "author", "");
    b.inputGainDb = getOr<double>(j, "input_gain_db", 0.0);

    if (j.contains("chain")) {
        const auto& chain = j["chain"];
        if (!chain.is_array())
            fail("board 'chain' must be an array");
        for (std::size_t i = 0; i < chain.size(); ++i)
            b.chain.push_back(instanceFromJson(chain[i], i));
    }

    if (j.contains("pedals") && !j["pedals"].is_null()) {
        const auto& pedals = j["pedals"];
        if (!pedals.is_object())
            fail("board 'pedals' must be an object keyed by pedal id");
        for (auto it = pedals.begin(); it != pedals.end(); ++it) {
            if (!it.value().is_object())
                fail("embedded pedal '" + it.key() + "' must be an object");
            b.embeddedPedals[it.key()] = it.value();
        }
    }
    return b;
}

Board boardFromJsonString(const std::string& text)
{
    json j;
    try {
        j = json::parse(text);
    } catch (const json::parse_error& e) {
        fail(std::string("board is not valid JSON: ") + e.what());
    }
    return boardFromJson(j);
}

json boardToJson(const Board& board, bool includeEmbeddedPedals)
{
    json j;
    j["schema"] = kBoardSchemaVersion;
    j["name"] = board.name;
    j["author"] = board.author;
    j["input_gain_db"] = board.inputGainDb;

    json chain = json::array();
    for (const auto& inst : board.chain) {
        json p;
        p["pedal"] = inst.pedalId;
        p["version"] = inst.versionReq;
        p["enabled"] = inst.enabled;
        json params = json::object();
        for (const auto& [k, v] : inst.params)
            params[k] = v;
        p["params"] = params;
        chain.push_back(p);
    }
    j["chain"] = chain;

    if (includeEmbeddedPedals && !board.embeddedPedals.empty()) {
        json pedals = json::object();
        for (const auto& [id, def] : board.embeddedPedals)
            pedals[id] = def;
        j["pedals"] = pedals;
    }
    return j;
}

std::string boardToJsonString(const Board& board, bool includeEmbeddedPedals, int indent)
{
    return boardToJson(board, includeEmbeddedPedals).dump(indent);
}

} // namespace openpedal
