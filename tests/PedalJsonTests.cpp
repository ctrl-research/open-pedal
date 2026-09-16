#include "TestHelpers.h"

#include "core/PedalDefinition.h"

#include <catch2/catch_test_macros.hpp>

using namespace openpedal;

namespace {
const char* kMinimal = R"({
  "id": "t.min", "nodes": [], "connections": [["in", "output"]]
})";
}

TEST_CASE("minimal passthrough pedal parses with defaults")
{
    auto r = parsePedalString(kMinimal);
    REQUIRE(r.ok());
    const auto& d = r.definition->descriptor;
    CHECK(d.id == "t.min");
    CHECK(d.name == "t.min");
    CHECK(d.version == "1.0.0");
    CHECK(d.category == "utility");
    CHECK(d.oversampling == 1);
    CHECK(d.params.empty());
    REQUIRE(r.definition->connections.size() == 1);
    CHECK(r.definition->connections[0].from == "in");
    CHECK(r.definition->connections[0].toNode == "output");
    CHECK(r.definition->connections[0].toPort.empty());
}

TEST_CASE("knobs parse into descriptors with enum and bool handling")
{
    auto r = parsePedalString(R"({
      "id": "t.knobs",
      "knobs": [
        {"id": "cut", "name": "Cutoff", "type": "float", "min": 100, "max": 8000, "default": 1000, "unit": "Hz", "taper": "log", "smoothing_ms": 50},
        {"id": "mode", "type": "enum", "values": ["a", "b", "c"], "default": "c"},
        {"id": "on", "type": "bool", "default": true},
        {"id": "steps", "type": "int", "min": 1, "max": 8}
      ],
      "nodes": [], "connections": [["in", "output"]]
    })");
    REQUIRE(r.ok());
    const auto& p = r.definition->descriptor.params;
    REQUIRE(p.size() == 4);
    CHECK(p[0].name == "Cutoff");
    CHECK(p[0].taper == Taper::Log);
    CHECK(p[0].smoothingMs == 50);
    CHECK(p[1].type == ParamType::Enum);
    CHECK(p[1].max == 2);
    CHECK(p[1].defaultValue == 2);
    CHECK(p[1].smoothingMs == 0);
    CHECK(p[2].type == ParamType::Bool);
    CHECK(p[2].defaultValue == 1);
    CHECK(p[3].defaultValue == 1); // defaults to min
    CHECK(p[3].smoothingMs == 0);
}

TEST_CASE("node params parse as literals, labels, and bindings")
{
    auto r = parsePedalString(R"({
      "id": "t.nodes",
      "knobs": [{"id": "k", "type": "float", "min": 0, "max": 1}],
      "nodes": [{"id": "n", "type": "gain", "gain": 2, "gain_db": {"knob": "k", "scale": 12, "offset": -6}}],
      "connections": [["in", "n"], ["n", "output"], ["n", "x.port"]]
    })");
    REQUIRE(r.ok());
    const auto& n = r.definition->nodes.at(0);
    CHECK(std::get<double>(n.params.at("gain")) == 2);
    const auto& b = std::get<KnobBinding>(n.params.at("gain_db"));
    CHECK(b.knobId == "k");
    CHECK(b.scale == 12);
    CHECK(b.offset == -6);
    CHECK(r.definition->connections[2].toNode == "x");
    CHECK(r.definition->connections[2].toPort == "port");
}

TEST_CASE("parser collects every error instead of stopping at the first")
{
    auto r = parsePedalString(R"({
      "id": "bad id!",
      "oversampling": 3,
      "tail_seconds": -1,
      "knobs": [
        {"id": "a", "type": "float", "min": 0, "max": 1},
        {"id": "a", "type": "float", "min": 0, "max": 2},
        {"id": "b", "type": "float", "min": 1, "max": 0},
        {"id": "e", "type": "enum", "values": ["x"]},
        {"id": "z", "type": "float", "min": 0, "max": 100, "taper": "log"},
        {"id": "q", "type": "float", "min": 0, "max": 1, "default": 3}
      ],
      "nodes": [
        {"id": "in", "type": "gain"},
        {"id": "n1", "type": "gain"},
        {"id": "n1", "type": "gain", "gain": [1]},
        {"type": "gain"}
      ],
      "connections": [["in"], "nope"]
    })");
    REQUIRE_FALSE(r.ok());
    CHECK(test::contains(r.errors, "pedal needs an 'id'"));
    CHECK(test::contains(r.errors, "oversampling must be 1, 2, 4 or 8"));
    CHECK(test::contains(r.errors, "tail_seconds must be >= 0"));
    CHECK(test::contains(r.errors, "duplicate knob id 'a'"));
    CHECK(test::contains(r.errors, "max <= min"));
    CHECK(test::contains(r.errors, "at least two labels"));
    CHECK(test::contains(r.errors, "log taper"));
    CHECK(test::contains(r.errors, "outside its range"));
    CHECK(test::contains(r.errors, "node id 'in' is reserved"));
    CHECK(test::contains(r.errors, "duplicate node id 'n1'"));
    CHECK(test::contains(r.errors, "param 'gain' must be a number"));
    CHECK(test::contains(r.errors, "nodes[3] needs an 'id'"));
    CHECK(test::contains(r.errors, "connections[0] must be"));
    CHECK(test::contains(r.errors, "connections[1] must be"));
    CHECK(r.errors.size() >= 14);
}

TEST_CASE("structural problems are reported")
{
    CHECK(test::contains(parsePedalString("{").errors, "not valid JSON"));
    CHECK(test::contains(parsePedalString("[]").errors, "must be a JSON object"));
    CHECK(test::contains(parsePedalString(R"({"id":"a.b"})").errors, "'nodes' must be an array"));
    CHECK(test::contains(parsePedalString(R"({"id":"a.b","nodes":[]})").errors, "'connections' must be an array"));
    CHECK(test::contains(parsePedalString(R"({"id":"a.b","schema":9,"nodes":[],"connections":[]})").errors, "newer than this build"));
    CHECK(test::contains(parsePedalString(R"({"id":"a.b","nodes":[],"connections":[],"knobs":[{"id":"k","type":"knob"}]})").errors, "unknown type 'knob'"));
    CHECK(test::contains(parsePedalString(R"({"id":"a.b","nodes":[],"connections":[],"knobs":[{"id":"k","type":"float","min":0,"max":1,"taper":"exp"}]})").errors, "unknown taper 'exp'"));
    CHECK(test::contains(parsePedalString(R"({"id":"a.b","nodes":[],"connections":[],"knobs":[{"id":"k","type":"enum","values":["a","b"],"default":"c"}]})").errors, "not one of its values"));
    CHECK(test::contains(parsePedalString(R"({"id":"a.b","nodes":[{"id":"n","type":"gain","gain":{"scale":2}}],"connections":[]})").errors, "binding needs a 'knob' id"));
}
