#include "core/ParamDescriptor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace openpedal;
using Catch::Matchers::WithinAbs;

TEST_CASE("linear taper maps ends and midpoint")
{
    ParamDescriptor p{.id = "level", .min = -24, .max = 12, .defaultValue = 0};
    CHECK_THAT(p.toNormalised(-24), WithinAbs(0.0, 1e-12));
    CHECK_THAT(p.toNormalised(12), WithinAbs(1.0, 1e-12));
    CHECK_THAT(p.toReal(0.5), WithinAbs(-6.0, 1e-12));
    CHECK_THAT(p.toReal(p.toNormalised(3.3)), WithinAbs(3.3, 1e-9));
}

TEST_CASE("log taper is equal travel per octave")
{
    ParamDescriptor p{.id = "cutoff", .min = 100, .max = 1600, .defaultValue = 400, .taper = Taper::Log};
    // 100 -> 1600 is four octaves; 400 is two octaves up, so halfway.
    CHECK_THAT(p.toNormalised(400), WithinAbs(0.5, 1e-12));
    CHECK_THAT(p.toReal(0.25), WithinAbs(200.0, 1e-9));
    CHECK_THAT(p.toReal(p.toNormalised(731.0)), WithinAbs(731.0, 1e-9));
}

TEST_CASE("audio taper gives more resolution at the bottom")
{
    ParamDescriptor p{.id = "mix", .min = 0, .max = 1, .defaultValue = 0.5, .taper = Taper::Audio};
    CHECK_THAT(p.toReal(0.5), WithinAbs(0.25, 1e-12));
    CHECK_THAT(p.toNormalised(0.25), WithinAbs(0.5, 1e-12));
}

TEST_CASE("values clamp and discrete types round")
{
    ParamDescriptor f{.id = "f", .min = 0, .max = 10};
    CHECK(f.clamp(-5) == 0);
    CHECK(f.clamp(50) == 10);
    CHECK(f.toReal(2.0) == 10);

    ParamDescriptor i{.id = "i", .type = ParamType::Int, .min = 0, .max = 4};
    CHECK(i.clamp(2.6) == 3);

    ParamDescriptor e{.id = "e", .type = ParamType::Enum, .min = 0, .max = 1,
                      .enumValues = {"symmetric", "asymmetric"}};
    CHECK(e.enumIndex("asymmetric") == 1);
    CHECK_FALSE(e.enumIndex("nope").has_value());
    CHECK(e.enumLabel(1) == "asymmetric");
    CHECK(e.enumLabel(0.4) == "symmetric");
}

TEST_CASE("validate rejects inconsistent descriptors")
{
    std::string err;
    CHECK(ParamDescriptor{.id = "ok", .min = 0, .max = 1}.validate(err));

    CHECK_FALSE(ParamDescriptor{.id = "", .min = 0, .max = 1}.validate(err));
    CHECK_FALSE(ParamDescriptor{.id = "x", .min = 1, .max = 1}.validate(err));
    CHECK_FALSE(ParamDescriptor{.id = "x", .min = 0, .max = 100, .taper = Taper::Log}.validate(err));
    CHECK(err.find("log taper") != std::string::npos);
    CHECK_FALSE(ParamDescriptor{.id = "x", .min = 0, .max = 1, .defaultValue = 2}.validate(err));
    CHECK_FALSE(ParamDescriptor{.id = "x", .type = ParamType::Enum, .min = 0, .max = 1,
                                .enumValues = {"only"}}.validate(err));
}

TEST_CASE("type and taper names round-trip")
{
    for (auto t : {ParamType::Float, ParamType::Int, ParamType::Bool, ParamType::Enum})
        CHECK(parseParamType(toString(t)) == t);
    for (auto t : {Taper::Linear, Taper::Log, Taper::Audio})
        CHECK(parseTaper(toString(t)) == t);
    CHECK_FALSE(parseTaper("exponential").has_value());
}
