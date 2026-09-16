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
