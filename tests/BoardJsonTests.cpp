#include "core/Board.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace openpedal;
using nlohmann::json;

namespace {

Board sampleBoard()
{
    Board b;
    b.name = "Lead";
    b.author = "jon";
    b.inputGainDb = -3.0;
    PedalInstance drive{.pedalId = "jon.ts-drive", .versionReq = "1.x", .enabled = true};
    drive.params["drive"] = 30;
    drive.params["tone"] = 1800.5;
    drive.params["clip"] = "asymmetric";
    PedalInstance delay{.pedalId = "openpedal.delay", .versionReq = "*", .enabled = false};
    delay.params["time_ms"] = 380;
    delay.params["sync"] = true;
    b.chain = {drive, delay};
    b.embeddedPedals["jon.ts-drive"] = json{{"id", "jon.ts-drive"}, {"name", "TS Drive"}};
    return b;
}

} // namespace

TEST_CASE("board round-trips through JSON with embedded pedals")
{
    const Board b = sampleBoard();
    const Board back = boardFromJsonString(boardToJsonString(b, true));
    CHECK(back == b);
}

TEST_CASE("export without embedded pedals strips the pedals object")
{
    const json j = boardToJson(sampleBoard(), false);
    CHECK_FALSE(j.contains("pedals"));
    CHECK(j["schema"] == kBoardSchemaVersion);
    CHECK(j["chain"].size() == 2);
    CHECK(j["chain"][0]["params"]["clip"] == "asymmetric");
}

TEST_CASE("minimal board parses with defaults")
{
    const Board b = boardFromJsonString(R"({"chain":[{"pedal":"a.b"}]})");
    CHECK(b.schema == 1);
    CHECK(b.inputGainDb == 0.0);
    REQUIRE(b.chain.size() == 1);
    CHECK(b.chain[0].versionReq == "*");
    CHECK(b.chain[0].enabled);
    CHECK(b.chain[0].params.empty());
}

TEST_CASE("unknown top-level keys are ignored for forward compatibility")
{
    const Board b = boardFromJsonString(R"({"schema":1,"future_thing":{"x":1},"chain":[]})");
    CHECK(b.chain.empty());
}

TEST_CASE("malformed boards fail with readable errors")
{
    CHECK_THROWS_WITH(boardFromJsonString("not json"), Catch::Matchers::ContainsSubstring("not valid JSON"));
    CHECK_THROWS_WITH(boardFromJsonString(R"([1,2])"), Catch::Matchers::ContainsSubstring("must be a JSON object"));
    CHECK_THROWS_WITH(boardFromJsonString(R"({"schema":99})"), Catch::Matchers::ContainsSubstring("newer than this build"));
    CHECK_THROWS_WITH(boardFromJsonString(R"({"chain":{}})"), Catch::Matchers::ContainsSubstring("'chain' must be an array"));
    CHECK_THROWS_WITH(boardFromJsonString(R"({"chain":[{"enabled":true}]})"), Catch::Matchers::ContainsSubstring("chain[0] is missing a 'pedal' id"));
    CHECK_THROWS_WITH(boardFromJsonString(R"({"chain":[{"pedal":"a.b","params":{"x":[1]}}]})"), Catch::Matchers::ContainsSubstring("chain[0].params.x"));
    CHECK_THROWS_WITH(boardFromJsonString(R"({"input_gain_db":"loud"})"), Catch::Matchers::ContainsSubstring("wrong type"));
    CHECK_THROWS_WITH(boardFromJsonString(R"({"pedals":[]})"), Catch::Matchers::ContainsSubstring("'pedals' must be an object"));
}

TEST_CASE("groups round-trip and validate")
{
    Board b;
    b.groups = {PedalGroup{.id = "g1", .name = "Lead", .enabled = false}, PedalGroup{.id = "g2", .name = "Ambient"}};
    b.chain.push_back(PedalInstance{.pedalId = "a.b", .group = "g1"});
    b.chain.push_back(PedalInstance{.pedalId = "c.d"});
    b.chain.push_back(PedalInstance{.pedalId = "e.f", .enabled = false, .group = "g2"});

    const json j = boardToJson(b, false);
    CHECK(j["groups"].size() == 2);
    CHECK(j["chain"][0]["group"] == "g1");
    CHECK_FALSE(j["chain"][1].contains("group"));

    const Board back = boardFromJson(j);
    CHECK(back == b);
    CHECK(back.groupIndex("g2") == 1);
    CHECK(back.groupIndex("zz") == -1);
    CHECK_FALSE(back.isActive(0)); // group off
    CHECK(back.isActive(1));       // no group
    CHECK_FALSE(back.isActive(2)); // pedal off

    CHECK_THROWS_WITH(boardFromJsonString(R"({"chain":[{"pedal":"a.b","group":"nope"}]})"),
                      Catch::Matchers::ContainsSubstring("unknown group 'nope'"));
    CHECK_THROWS_WITH(boardFromJsonString(R"({"groups":[{"id":"x"},{"id":"x"}]})"),
                      Catch::Matchers::ContainsSubstring("duplicate group id 'x'"));
    CHECK_THROWS_WITH(boardFromJsonString(R"({"groups":[{"name":"no id"}]})"),
                      Catch::Matchers::ContainsSubstring("needs an 'id'"));
    CHECK_THROWS_WITH(boardFromJsonString(R"({"groups":[{"id":"1"},{"id":"2"},{"id":"3"},{"id":"4"},{"id":"5"}]})"),
                      Catch::Matchers::ContainsSubstring("at most 4 groups"));
    CHECK(boardFromJsonString(R"({"groups":[{"id":"only"}]})").groups[0].name == "only");
}
