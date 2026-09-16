// Invariants every shipped example pedal must satisfy. These double as the checks a pedal
// author can run over their own folder by pointing OPENPEDAL_EXAMPLE_PEDALS_DIR elsewhere.
#include "TestHelpers.h"

#include "core/Board.h"
#include "core/Chain.h"
#include "core/PedalCollection.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace openpedal;

namespace {

PedalCollection& examples()
{
    static PedalCollection c = [] {
        PedalCollection col;
        col.loadDirectory(OPENPEDAL_EXAMPLE_PEDALS_DIR, "bundled");
        return col;
    }();
    return c;
}

double knobExtreme(const ParamDescriptor& p, int which)
{
    return which == 0 ? p.min : p.max;
}

} // namespace

TEST_CASE("all example pedals load without errors")
{
    auto& c = examples();
    for (const auto& e : c.loadErrors())
        for (const auto& m : e.messages)
            UNSCOPED_INFO(e.source + ": " + m);
    CHECK(c.loadErrors().empty());
    for (const char* id : {"openpedal.clean-boost", "openpedal.ts-drive", "openpedal.chorus", "openpedal.digital-delay"})
        CHECK(c.find(id) != nullptr);
}

TEST_CASE("example pedals satisfy the basic invariants")
{
    auto& c = examples();
    for (const auto* entry : c.all()) {
        const auto& d = entry->definition.descriptor;
        DYNAMIC_SECTION(d.id)
        {
            std::vector<std::string> errors;
            auto pedal = c.instantiate(d.id, "*", errors);
            REQUIRE(pedal);
            const double sr = 48000;
            pedal->prepare(sr, 256);

            // Silence in, silence out (allow denormal-level noise).
            std::vector<float> silence(4096, 0.0f);
            test::processInBlocks(*pedal, silence, 256);
            CHECK(test::rms(silence) < 1e-6);

            // Signal in, finite and non-trivial signal out that differs from the input.
            pedal->reset();
            auto sig = test::sine(220, sr, 8192, 0.3f);
            const auto orig = sig;
            test::processInBlocks(*pedal, sig, 256);
            CHECK(test::allFinite(sig));
            CHECK(test::rms(sig, 2048) > 1e-3);
            CHECK(sig != orig);

            // Every knob at both extremes: still finite, still bounded.
            for (const auto& p : d.params) {
                for (int which = 0; which < 2; ++which) {
                    pedal->setParam(p.id, knobExtreme(p, which));
                    auto buf = test::sine(220, sr, 4096, 0.3f);
                    test::processInBlocks(*pedal, buf, 256);
                    CHECK(test::allFinite(buf));
                    CHECK(test::rms(buf) < 20.0);
                }
                pedal->setParam(p.id, p.defaultValue);
            }
        }
    }
}

TEST_CASE("starter board renders through a chain built from the collection")
{
    std::ifstream in(std::filesystem::path(OPENPEDAL_EXAMPLE_BOARDS_DIR) / "starter.json");
    std::stringstream ss;
    ss << in.rdbuf();
    const Board board = boardFromJsonString(ss.str());
    REQUIRE(board.chain.size() == 4);

    auto& c = examples();
    Chain chain;
    chain.setInputGainDb(board.inputGainDb);
    for (const auto& inst : board.chain) {
        std::vector<std::string> errors;
        auto pedal = c.instantiate(inst.pedalId, inst.versionReq, errors);
        REQUIRE(pedal);
        for (const auto& [k, v] : inst.params) {
            const auto* desc = pedal->descriptor().findParam(k);
            REQUIRE(desc != nullptr);
            if (v.is_string())
                pedal->setParam(k, static_cast<double>(desc->enumIndex(v.get<std::string>()).value()));
            else
                pedal->setParam(k, v.get<double>());
        }
        chain.addSlot(std::move(pedal), inst.pedalId, inst.enabled);
    }
    chain.prepare(48000, 512);
    auto buf = test::sine(196, 48000, 48000, 0.3f);
    const auto orig = buf;
    for (int pos = 0; pos < 48000; pos += 512)
        chain.process(buf.data() + pos, std::min(512, 48000 - pos));
    CHECK(test::allFinite(buf));
    CHECK(buf != orig);
    CHECK(test::rms(buf, 4096) > 0.01);
    CHECK(chain.latencySamples() > 0); // ts-drive is oversampled
}
