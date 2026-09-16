#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace openpedal;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("passthrough graph is bit-exact")
{
    auto p = test::compilePedal(R"({"id":"t.pass","nodes":[],"connections":[["in","output"]]})");
    p->prepare(48000, 64);
    auto buf = test::sine(440, 48000, 256);
    const auto orig = buf;
    test::processInBlocks(*p, buf);
    CHECK(buf == orig);
    CHECK(p->latencySamples() == 0);
}

TEST_CASE("chained gains multiply and knob bindings apply scale and offset")
{
    auto p = test::compilePedal(R"({
      "id":"t.gain",
      "knobs":[{"id":"amount","type":"float","min":0,"max":10,"default":2,"smoothing_ms":0}],
      "nodes":[
        {"id":"a","type":"gain","gain":{"knob":"amount"}},
        {"id":"b","type":"gain","gain":{"knob":"amount","scale":0.5,"offset":1}}
      ],
      "connections":[["in","a"],["a","b"],["b","output"]]
    })");
    p->prepare(48000, 32);
    std::vector<float> buf(32, 1.0f);
    p->process(buf.data(), 32);
    CHECK_THAT(static_cast<double>(buf[31]), WithinAbs(2.0 * (0.5 * 2 + 1), 1e-5)); // 4
    CHECK(p->getParam("amount") == 2.0);

    p->setParam("amount", 4.0);
    std::fill(buf.begin(), buf.end(), 1.0f);
    p->process(buf.data(), 32);
    CHECK_THAT(static_cast<double>(buf[31]), WithinAbs(4.0 * (0.5 * 4 + 1), 1e-5)); // 12
}

TEST_CASE("multiple connections into one port sum")
{
    auto p = test::compilePedal(R"({
      "id":"t.sum",
      "nodes":[{"id":"a","type":"gain","gain":2},{"id":"b","type":"gain","gain":3},{"id":"out","type":"gain"}],
      "connections":[["in","a"],["in","b"],["a","out"],["b","out"],["out","output"],["in","output"]]
    })");
    p->prepare(48000, 8);
    std::vector<float> buf(8, 1.0f);
    p->process(buf.data(), 8);
    CHECK_THAT(static_cast<double>(buf[7]), WithinAbs(2.0 + 3.0 + 1.0, 1e-6));
}

TEST_CASE("feedback through a delay is allowed and produces repeats")
{
    auto p = test::compilePedal(R"({
      "id":"t.fb",
      "nodes":[
        {"id":"dly","type":"delay","time_ms":1,"max_ms":10,"interpolation":"linear"},
        {"id":"fb","type":"gain","gain":0.5}
      ],
      "connections":[["in","dly"],["dly","fb"],["fb","dly"],["dly","output"]]
    })");
    const double sr = 48000;
    p->prepare(sr, 64);
    auto buf = test::impulse(400);
    test::processInBlocks(*p, buf);
    const int d = 48; // 1 ms
    CHECK_THAT(static_cast<double>(buf[static_cast<std::size_t>(d)]), WithinAbs(1.0, 1e-4));
    // The feedback edge is delayed by one sample, so the second echo lands at 2d + 1.
    CHECK_THAT(static_cast<double>(buf[static_cast<std::size_t>(2 * d + 1)]), WithinAbs(0.5, 1e-4));
    CHECK_THAT(static_cast<double>(buf[static_cast<std::size_t>(3 * d + 2)]), WithinAbs(0.25, 1e-4));
    CHECK(test::allFinite(buf));
}

TEST_CASE("oversampled pedal reports latency and stays finite")
{
    auto p = test::compilePedal(R"({
      "id":"t.os","oversampling":4,
      "knobs":[{"id":"drive","type":"float","min":1,"max":100,"default":50}],
      "nodes":[{"id":"ws","type":"waveshaper","shape":"hard","drive":{"knob":"drive"}}],
      "connections":[["in","ws"],["ws","output"]]
    })");
    p->prepare(48000, 128);
    CHECK(p->latencySamples() > 0);
    auto buf = test::sine(1000, 48000, 4096);
    test::processInBlocks(*p, buf, 128);
    CHECK(test::allFinite(buf));
    // Hard clip at drive 50 turns a 0.5 sine into a near square: RMS approaches 1.
    CHECK(test::rms(buf, 512) > 0.9);
    CHECK(test::rms(buf, 512) < 1.05);
}

TEST_CASE("knob smoothing ramps instead of jumping")
{
    auto p = test::compilePedal(R"({
      "id":"t.smooth",
      "knobs":[{"id":"g","type":"float","min":0,"max":1,"default":0,"smoothing_ms":10}],
      "nodes":[{"id":"a","type":"gain","gain":{"knob":"g"}}],
      "connections":[["in","a"],["a","output"]]
    })");
    p->prepare(48000, 64);
    p->setParam("g", 1.0);
    std::vector<float> buf(1024, 1.0f); // ~21 ms
    test::processInBlocks(*p, buf);
    CHECK(buf[0] < 0.2f);      // still near 0 at the start
    CHECK(buf[240] > 0.2f);    // partway at 5 ms
    CHECK(buf[240] < 0.8f);
    CHECK_THAT(static_cast<double>(buf[1023]), WithinAbs(1.0, 1e-5)); // settled
}

TEST_CASE("enum knob maps labels onto block options")
{
    auto p = test::compilePedal(R"({
      "id":"t.enum",
      "knobs":[{"id":"mode","type":"enum","values":["Warm","Cold"],"default":"Warm"}],
      "nodes":[{"id":"ws","type":"waveshaper","drive":100,"shape":{"knob":"mode","map":{"Warm":"tanh","Cold":"hard"}}}],
      "connections":[["in","ws"],["ws","output"]]
    })");
    p->prepare(48000, 8);
    std::vector<float> buf(8, 0.02f);
    p->process(buf.data(), 8);
    CHECK_THAT(static_cast<double>(buf[7]), WithinAbs(std::tanh(2.0), 1e-4));
    p->setParam("mode", 1);
    std::fill(buf.begin(), buf.end(), 0.02f);
    p->process(buf.data(), 8);
    CHECK_THAT(static_cast<double>(buf[7]), WithinAbs(1.0, 1e-5));
}

TEST_CASE("compile errors name the offending node and give options")
{
    const std::string head = R"({"id":"t.err","knobs":[{"id":"k","type":"float","min":0,"max":1},{"id":"e","type":"enum","values":["x","y"]}],)";

    CHECK(test::contains(test::compileErrors(head + R"("nodes":[{"id":"n","type":"reverb"}],"connections":[["in","n"],["n","output"]]})"),
                         "node 'n' has unknown block type 'reverb'"));
    CHECK(test::contains(test::compileErrors(head + R"("nodes":[{"id":"n","type":"gain","level":1}],"connections":[["in","n"],["n","output"]]})"),
                         "param 'level' does not exist on block 'gain' (has: gain, gain_db)"));
    CHECK(test::contains(test::compileErrors(head + R"("nodes":[{"id":"n","type":"gain","gain":-5}],"connections":[["in","n"],["n","output"]]})"),
                         "outside"));
    CHECK(test::contains(test::compileErrors(head + R"("nodes":[{"id":"n","type":"filter","mode":"comb"}],"connections":[["in","n"],["n","output"]]})"),
                         "unknown option \"comb\" (options: lowpass, highpass"));
    CHECK(test::contains(test::compileErrors(head + R"("nodes":[{"id":"n","type":"gain","gain":"loud"}],"connections":[["in","n"],["n","output"]]})"),
                         "is numeric but was given the text \"loud\""));
    CHECK(test::contains(test::compileErrors(head + R"("nodes":[{"id":"n","type":"gain","gain":{"knob":"nope"}}],"connections":[["in","n"],["n","output"]]})"),
                         "bound to unknown knob 'nope'"));
    CHECK(test::contains(test::compileErrors(head + R"("nodes":[{"id":"n","type":"filter","mode":{"knob":"k"}}],"connections":[["in","n"],["n","output"]]})"),
                         "must be an enum knob"));
    CHECK(test::contains(test::compileErrors(head + R"("nodes":[{"id":"n","type":"filter","mode":{"knob":"e"}}],"connections":[["in","n"],["n","output"]]})"),
                         "value \"x\" maps to \"x\", which is not an option"));
    CHECK(test::contains(test::compileErrors(head + R"("nodes":[{"id":"n","type":"gain","gain":{"knob":"e"}}],"connections":[["in","n"],["n","output"]]})"),
                         "is numeric but knob 'e' is an enum"));
    CHECK(test::contains(test::compileErrors(head + R"("nodes":[{"id":"n","type":"gain"}],"connections":[["in","n"],["n","output"],["ghost","n"]]})"),
                         "unknown source 'ghost'"));
    CHECK(test::contains(test::compileErrors(head + R"("nodes":[{"id":"n","type":"gain"}],"connections":[["in","n"],["n","output"],["n","ghost"]]})"),
                         "unknown target 'ghost'"));
    CHECK(test::contains(test::compileErrors(head + R"("nodes":[{"id":"n","type":"mix"}],"connections":[["in","n.c"],["n","output"]]})"),
                         "has no input 'c' (has: a, b)"));
    CHECK(test::contains(test::compileErrors(head + R"("nodes":[{"id":"n","type":"lfo"}],"connections":[["in","n"],["n","output"]]})"),
                         "block 'lfo' has no inputs"));
    CHECK(test::contains(test::compileErrors(head + R"("nodes":[{"id":"n","type":"gain"}],"connections":[["in","n"]]})"),
                         "nothing is connected to 'output'"));
    CHECK(test::contains(test::compileErrors(head + R"("nodes":[{"id":"n","type":"gain"}],"connections":[["n","in"],["n","output"]]})"),
                         "nothing can feed into 'in'"));
    CHECK(test::contains(test::compileErrors(head + R"("nodes":[{"id":"a","type":"gain"},{"id":"b","type":"gain"}],"connections":[["in","a"],["a","b"],["b","a"],["b","output"]]})"),
                         "feedback loop that does not pass through a 'delay' node (involving: a, b)"));
}
