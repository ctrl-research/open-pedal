#include "TestHelpers.h"

#include "core/graph/Block.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <string>

using namespace openpedal;
using namespace openpedal::graph;
using Catch::Matchers::WithinAbs;

namespace {

std::unique_ptr<Block> make(const std::string& type)
{
    ensureBuiltinBlocksRegistered();
    auto b = BlockRegistry::instance().create(type);
    REQUIRE(b != nullptr);
    return b;
}

void set(Block& b, const std::string& name, double v)
{
    const int i = b.spec().paramIndex(name);
    REQUIRE(i >= 0);
    b.setParam(i, v);
}

// Run a block on a sine and return the steady-state RMS gain relative to the input.
double gainAt(Block& b, double freq, double sr = 48000)
{
    auto in = test::sine(freq, sr, 8192, 1.0f);
    std::vector<float> out(in.size());
    for (std::size_t i = 0; i < in.size(); ++i)
        out[i] = b.tick(&in[i]);
    return test::rms(out, 4096) / test::rms(in, 4096);
}

} // namespace

TEST_CASE("registry lists all built-in blocks with specs")
{
    ensureBuiltinBlocksRegistered();
    const auto all = BlockRegistry::instance().all();
    std::vector<std::string> names;
    for (auto* s : all)
        names.push_back(s->type);
    for (const char* expected : {"clamp", "dc_block", "delay", "envelope", "filter", "gain", "lfo", "mix", "multiply", "tilt", "waveshaper"})
        CHECK(std::find(names.begin(), names.end(), expected) != names.end());
    for (auto* s : all) {
        CHECK_FALSE(s->doc.empty());
        for (const auto& p : s->params) {
            CHECK(p.defaultValue >= p.min);
            CHECK(p.defaultValue <= p.max);
            if (p.isEnum())
                CHECK(p.max == static_cast<double>(p.enumLabels.size() - 1));
        }
    }
    CHECK(BlockRegistry::instance().find("nope") == nullptr);
    CHECK(BlockRegistry::instance().create("nope") == nullptr);
}

TEST_CASE("gain combines linear and dB")
{
    auto g = make("gain");
    g->prepare(48000);
    set(*g, "gain", 2.0);
    set(*g, "gain_db", 6.0206);
    const float in = 0.25f;
    CHECK_THAT(static_cast<double>(g->tick(&in)), WithinAbs(1.0, 1e-3));
}

TEST_CASE("filter modes shape the spectrum as expected")
{
    auto f = make("filter");
    f->prepare(48000);
    set(*f, "cutoff", 1000);
    set(*f, "q", 0.7071);

    SECTION("lowpass") {
        set(*f, "mode", 0);
        CHECK(gainAt(*f, 100) > 0.98);
        CHECK_THAT(gainAt(*f, 1000), WithinAbs(0.7071, 0.02));
        CHECK(gainAt(*f, 8000) < 0.03);
    }
    SECTION("highpass") {
        set(*f, "mode", 1);
        CHECK(gainAt(*f, 100) < 0.02);
        CHECK(gainAt(*f, 8000) > 0.98);
    }
    SECTION("bandpass peaks at centre") {
        set(*f, "mode", 2);
        CHECK_THAT(gainAt(*f, 1000), WithinAbs(1.0, 0.02));
        CHECK(gainAt(*f, 100) < 0.2);
    }
    SECTION("notch removes centre") {
        set(*f, "mode", 3);
        CHECK(gainAt(*f, 1000) < 0.05);
        CHECK(gainAt(*f, 100) > 0.95);
    }
    SECTION("peak boosts by gain_db") {
        set(*f, "mode", 4);
        set(*f, "gain_db", 12);
        CHECK_THAT(gainAt(*f, 1000), WithinAbs(3.98, 0.1));
        CHECK(gainAt(*f, 50) < 1.1);
    }
    SECTION("shelves") {
        set(*f, "gain_db", -12);
        set(*f, "mode", 5);
        CHECK_THAT(gainAt(*f, 50), WithinAbs(0.25, 0.03));
        CHECK(gainAt(*f, 10000) > 0.95);
        set(*f, "mode", 6);
        CHECK_THAT(gainAt(*f, 10000), WithinAbs(0.25, 0.03));
        CHECK(gainAt(*f, 50) > 0.95);
    }
}

TEST_CASE("tilt brightens for positive values")
{
    auto t = make("tilt");
    t->prepare(48000);
    set(*t, "center", 1000);
    set(*t, "tilt_db", 12);
    CHECK(gainAt(*t, 100) < 0.6);
    CHECK(gainAt(*t, 10000) > 1.6);
}

TEST_CASE("dc_block removes offset")
{
    auto d = make("dc_block");
    d->prepare(48000);
    float last = 1.0f;
    for (int i = 0; i < 48000; ++i) {
        const float in = 1.0f;
        last = d->tick(&in);
    }
    CHECK(std::fabs(last) < 1e-3f);
}

TEST_CASE("waveshaper shapes clip as documented")
{
    auto w = make("waveshaper");
    w->prepare(48000);
    set(*w, "drive", 1.0);
    const float big = 10.0f, neg = -10.0f, small = 0.1f;

    set(*w, "shape", 2); // hard
    CHECK(w->tick(&big) == 1.0f);
    CHECK(w->tick(&neg) == -1.0f);
    CHECK_THAT(static_cast<double>(w->tick(&small)), WithinAbs(0.1, 1e-6));

    set(*w, "shape", 0); // tanh
    CHECK_THAT(static_cast<double>(w->tick(&big)), WithinAbs(1.0, 1e-4));
    CHECK_THAT(static_cast<double>(w->tick(&small)), WithinAbs(std::tanh(0.1), 1e-6));

    set(*w, "shape", 1); // soft
    CHECK_THAT(static_cast<double>(w->tick(&big)), WithinAbs(1.0, 1e-6));
    CHECK_THAT(static_cast<double>(w->tick(&small)), WithinAbs(0.1 - (4.0 / 27.0) * 0.001, 1e-6));

    set(*w, "shape", 3); // asymmetric: negative side has more headroom
    CHECK_THAT(static_cast<double>(w->tick(&big)), WithinAbs(1.0, 1e-4));
    CHECK_THAT(static_cast<double>(w->tick(&neg)), WithinAbs(-2.0, 1e-3));

    set(*w, "shape", 4); // fold
    const float x15 = 1.5f;
    CHECK_THAT(static_cast<double>(w->tick(&x15)), WithinAbs(0.5, 1e-6));

    set(*w, "shape", 5); // diode
    CHECK(w->tick(&big) > 0.99f);
    CHECK(w->tick(&big) <= 1.0f);

    // bias is compensated so silence stays silence
    set(*w, "shape", 0);
    set(*w, "bias", 0.5);
    const float zero = 0.0f;
    CHECK_THAT(static_cast<double>(w->tick(&zero)), WithinAbs(0.0, 1e-7));
}

TEST_CASE("delay delays by time_ms and follows mod input")
{
    auto d = make("delay");
    set(*d, "max_ms", 10);
    set(*d, "time_ms", 1);
    set(*d, "interpolation", 0);
    d->prepare(48000);

    std::vector<float> out(200);
    for (int i = 0; i < 200; ++i) {
        const float in[2] = {i == 0 ? 1.0f : 0.0f, 0.0f};
        out[static_cast<std::size_t>(i)] = d->tick(in);
    }
    CHECK_THAT(static_cast<double>(out[48]), WithinAbs(1.0, 1e-6));
    CHECK(out[47] == 0.0f);
    CHECK(out[49] == 0.0f);

    d->reset();
    for (int i = 0; i < 200; ++i) {
        const float in[2] = {i == 0 ? 1.0f : 0.0f, 1.0f}; // +1 ms via mod
        out[static_cast<std::size_t>(i)] = d->tick(in);
    }
    CHECK_THAT(static_cast<double>(out[96]), WithinAbs(1.0, 1e-6));

    SECTION("cubic interpolation of a fractional delay stays bounded")
    {
        set(*d, "interpolation", 1);
        set(*d, "time_ms", 0.51);
        d->reset();
        auto s = test::sine(1000, 48000, 2000, 0.5f);
        float peak = 0.0f;
        for (float x : s) {
            const float in[2] = {x, 0.0f};
            peak = std::max(peak, std::fabs(d->tick(in)));
        }
        CHECK(peak > 0.45f);
        CHECK(peak < 0.56f);
    }
}

TEST_CASE("lfo shapes stay within offset +- depth and run at rate")
{
    auto l = make("lfo");
    set(*l, "rate_hz", 100);
    set(*l, "depth", 2);
    set(*l, "offset", 1);
    l->prepare(48000);
    for (int shape = 0; shape < 4; ++shape) {
        set(*l, "shape", shape);
        l->reset();
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < 480; ++i) { // exactly one cycle
            const float v = l->tick(nullptr);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        CHECK(lo >= -1.0f - 1e-4f);
        CHECK(hi <= 3.0f + 1e-4f);
        CHECK(hi - lo > 3.9f);
    }
}

TEST_CASE("mix, multiply and clamp arithmetic")
{
    auto m = make("mix");
    set(*m, "mix", 0.25);
    const float ab[2] = {1.0f, 3.0f};
    CHECK_THAT(static_cast<double>(m->tick(ab)), WithinAbs(1.5, 1e-6));

    auto mul = make("multiply");
    CHECK_THAT(static_cast<double>(mul->tick(ab)), WithinAbs(3.0, 1e-6));

    auto c = make("clamp");
    set(*c, "min", -0.5);
    set(*c, "max", 0.5);
    const float big = 7.0f;
    CHECK(c->tick(&big) == 0.5f);
}

TEST_CASE("envelope follows level with attack and release")
{
    auto e = make("envelope");
    set(*e, "attack_ms", 1);
    set(*e, "release_ms", 10);
    e->prepare(48000);
    float v = 0.0f;
    for (int i = 0; i < 480; ++i) { // 10 ms of full-scale
        const float in = (i % 2) ? 1.0f : -1.0f;
        v = e->tick(&in);
    }
    CHECK(v > 0.99f);
    for (int i = 0; i < 480; ++i) { // 10 ms of silence: one release time constant
        const float in = 0.0f;
        v = e->tick(&in);
    }
    CHECK_THAT(static_cast<double>(v), WithinAbs(std::exp(-1.0), 0.02));
}
