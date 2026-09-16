#include "TestHelpers.h"

#include "core/PedalCollection.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace openpedal;

namespace {

std::string pedalJson(const std::string& id, const std::string& version, double gain = 1.0)
{
    return R"({"id":")" + id + R"(","version":")" + version + R"(","nodes":[{"id":"g","type":"gain","gain":)"
           + std::to_string(gain) + R"(}],"connections":[["in","g"],["g","output"]]})";
}

} // namespace

TEST_CASE("version requirements")
{
    CHECK(versionSatisfies("1.2.3", "*"));
    CHECK(versionSatisfies("1.2.3", ""));
    CHECK(versionSatisfies("1.2.3", "1.x"));
    CHECK(versionSatisfies("1.2.3", "1.*"));
    CHECK(versionSatisfies("1.2.3", "1.2.x"));
    CHECK(versionSatisfies("1.2.3", "1.2.3"));
    CHECK_FALSE(versionSatisfies("2.0.0", "1.x"));
    CHECK_FALSE(versionSatisfies("1.3.0", "1.2.x"));
    CHECK_FALSE(versionSatisfies("1.2.4", "1.2.3"));
    CHECK(compareVersions("1.10.0", "1.9.9") > 0);
    CHECK(compareVersions("1.0.0", "1.0.0") == 0);
    CHECK(compareVersions("0.9", "1.0.0") < 0);
}

TEST_CASE("collection resolves the highest matching version and instantiates fresh pedals")
{
    PedalCollection c;
    REQUIRE(c.addString(pedalJson("a.b", "1.0.0", 1.0), "user"));
    REQUIRE(c.addString(pedalJson("a.b", "1.4.0", 2.0), "user"));
    REQUIRE(c.addString(pedalJson("a.b", "2.0.0", 3.0), "user"));
    REQUIRE(c.loadErrors().empty());

    CHECK(c.find("a.b", "1.x")->definition.descriptor.version == "1.4.0");
    CHECK(c.find("a.b", "*")->definition.descriptor.version == "2.0.0");
    CHECK(c.find("a.b", "3.x") == nullptr);
    CHECK(c.find("nope") == nullptr);
    CHECK(c.all().size() == 3);

    std::vector<std::string> errors;
    auto p1 = c.instantiate("a.b", "1.x", errors);
    auto p2 = c.instantiate("a.b", "1.x", errors);
    REQUIRE(p1);
    REQUIRE(p2);
    CHECK(p1.get() != p2.get());
    CHECK(errors.empty());

    CHECK(c.instantiate("a.b", "9.x", errors) == nullptr);
    CHECK(test::contains(errors, "pedal 'a.b' (version 9.x) is not installed"));
}

TEST_CASE("re-adding the same id and version replaces it; removeOrigin drops a source")
{
    PedalCollection c;
    c.addString(pedalJson("a.b", "1.0.0", 1.0), "bundled");
    c.addString(pedalJson("a.b", "1.0.0", 5.0), "user");
    CHECK(c.all().size() == 1);
    CHECK(c.find("a.b")->origin == "user");
    c.removeOrigin("user");
    CHECK(c.find("a.b") == nullptr);
}

TEST_CASE("bad definitions are recorded as load errors, not thrown")
{
    PedalCollection c;
    CHECK_FALSE(c.addString("{{", "user"));
    CHECK_FALSE(c.addString(R"({"id":"x.y","nodes":[{"id":"n","type":"nope"}],"connections":[["in","n"],["n","output"]]})", "user"));
    REQUIRE(c.loadErrors().size() == 2);
    CHECK(test::contains(c.loadErrors()[0].messages, "not valid JSON"));
    CHECK(c.loadErrors()[1].source == "x.y");
    CHECK(test::contains(c.loadErrors()[1].messages, "unknown block type 'nope'"));
    c.clearLoadErrors();
    CHECK(c.loadErrors().empty());
}

TEST_CASE("loadDirectory reads every json file and tolerates a missing folder")
{
    const auto dir = std::filesystem::temp_directory_path() / "openpedal-test-pedals";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "one.json") << pedalJson("t.one", "1.0.0");
    std::ofstream(dir / "two.json") << pedalJson("t.two", "1.0.0");
    std::ofstream(dir / "broken.json") << "nope";
    std::ofstream(dir / "readme.txt") << "ignored";

    PedalCollection c;
    c.loadDirectory(dir);
    CHECK(c.find("t.one") != nullptr);
    CHECK(c.find("t.two") != nullptr);
    CHECK(c.find("t.one")->sourceFile.filename() == "one.json");
    REQUIRE(c.loadErrors().size() == 1);
    CHECK(c.loadErrors()[0].source.find("broken.json") != std::string::npos);

    c.loadDirectory(dir / "does-not-exist");
    CHECK(c.loadErrors().size() == 1);
    std::filesystem::remove_all(dir);
}
