#include "TestHelpers.h"

#include "core/BoardLoader.h"

#include <catch2/catch_test_macros.hpp>

using namespace openpedal;
using nlohmann::json;

namespace {

json gainPedal(const std::string& id, double gain)
{
    return json::parse(R"({"id":")" + id + R"(","knobs":[{"id":"g","type":"float","min":0,"max":10,"default":1,"smoothing_ms":0},
      {"id":"m","type":"enum","values":["a","b"]}],
      "nodes":[{"id":"n","type":"gain","gain":)" + std::to_string(gain) + R"(},{"id":"k","type":"gain","gain":{"knob":"g"}}],
      "connections":[["in","n"],["n","k"],["k","output"]]})");
}

} // namespace

TEST_CASE("buildChain resolves local, embedded, missing, and failed slots")
{
    PedalCollection c;
    REQUIRE(c.add(gainPedal("t.local", 2.0), "user"));

    Board b;
    b.chain.push_back(PedalInstance{.pedalId = "t.local", .params = {{"g", 3}, {"m", "b"}, {"ghost", 1}, {"m2", "zzz"}}});
    b.chain.push_back(PedalInstance{.pedalId = "t.embedded", .params = {{"g", 1}}});
    b.chain.push_back(PedalInstance{.pedalId = "t.missing"});
    b.chain.push_back(PedalInstance{.pedalId = "t.broken"});
    b.embeddedPedals["t.embedded"] = gainPedal("t.embedded", 5.0);
    b.embeddedPedals["t.broken"] = json::parse(R"({"id":"t.broken","nodes":[{"id":"x","type":"nope"}],"connections":[["in","x"],["x","output"]]})");

    ResolutionReport report;
    auto chain = buildChain(b, c, report);
    REQUIRE(chain->numSlots() == 4);
    REQUIRE(report.items.size() == 4);

    CHECK(report.items[0].status == ResolutionReport::Status::Local);
    CHECK(test::contains(report.items[0].warnings, "knob 'ghost' does not exist"));
    CHECK(test::contains(report.items[0].warnings, "knob 'm2' does not exist"));
    CHECK(chain->slot(0).pedal->getParam("g") == 3.0);
    CHECK(chain->slot(0).pedal->getParam("m") == 1.0);

    CHECK(report.items[1].status == ResolutionReport::Status::Embedded);
    CHECK(chain->slot(1).pedal != nullptr);

    CHECK(report.items[2].status == ResolutionReport::Status::Missing);
    CHECK(chain->slot(2).pedal == nullptr);

    CHECK(report.items[3].status == ResolutionReport::Status::Failed);
    CHECK(chain->slot(3).pedal == nullptr);
    CHECK(test::contains(report.items[3].errors, "unknown block type 'nope'"));

    CHECK_FALSE(report.allResolved());
    const auto summary = report.summary();
    CHECK(test::contains(summary, "slot 2 (t.embedded): not installed, using the definition embedded"));
    CHECK(test::contains(summary, "slot 3 (t.missing): pedal is not installed"));
    CHECK(test::contains(summary, "slot 4 (t.broken): pedal failed to load"));

    chain->prepare(48000, 16);
    std::vector<float> buf(16, 1.0f);
    chain->process(buf.data(), 16);
    CHECK(buf[15] == 2.0f * 3.0f * 5.0f * 1.0f);
}

TEST_CASE("a fully local board reports resolved and enum labels that do not exist are warnings")
{
    PedalCollection c;
    REQUIRE(c.add(gainPedal("t.local", 1.0), "user"));
    Board b;
    b.chain.push_back(PedalInstance{.pedalId = "t.local", .params = {{"m", "nope"}}});
    ResolutionReport report;
    auto chain = buildChain(b, c, report);
    CHECK(report.items[0].status == ResolutionReport::Status::Local);
    CHECK(test::contains(report.items[0].warnings, "has no option \"nope\""));
    CHECK_FALSE(report.allResolved());

    b.chain[0].params.clear();
    ResolutionReport clean;
    buildChain(b, c, clean);
    CHECK(clean.allResolved());
    CHECK(clean.summary().empty());
}

TEST_CASE("buildChain applies groups from the board")
{
    PedalCollection c;
    REQUIRE(c.add(gainPedal("t.local", 2.0), "user"));
    Board b;
    b.groups = {PedalGroup{.id = "g1", .name = "Lead", .enabled = false}, PedalGroup{.id = "g2", .name = "B"}};
    b.chain.push_back(PedalInstance{.pedalId = "t.local", .group = "g1"});
    b.chain.push_back(PedalInstance{.pedalId = "t.local", .group = "g2"});
    b.chain.push_back(PedalInstance{.pedalId = "t.local"});
    ResolutionReport report;
    auto chain = buildChain(b, c, report);
    CHECK(chain->slot(0).group == 0);
    CHECK(chain->slot(1).group == 1);
    CHECK(chain->slot(2).group == -1);
    CHECK_FALSE(chain->groupEnabled(0));
    CHECK_FALSE(chain->isActive(0));
    CHECK(chain->isActive(1));
    chain->prepare(48000, 8);
    std::vector<float> buf(8, 1.0f);
    chain->process(buf.data(), 8);
    CHECK(buf[7] == 4.0f); // slots 1 and 2 only
}
