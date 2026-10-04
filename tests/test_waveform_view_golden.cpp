#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <support/GoldenJson.hpp>
#include <support/WaveformScenario.hpp>

namespace detail = magda::sdk::detail;
namespace display = magda::sdk::display;
namespace golden = magda::sdk::golden_json;
namespace scenario = magda::sdk::waveform_scenario;

TEST_CASE("Waveform view frames match their golden display lists", "[waveview][golden]") {
    const std::string dir = std::string(MAGDA_SDK_TESTS_DIR) + "/golden/waveform-view/";
    std::string error;
    const auto scenarios = detail::parseJson(golden::readFile(dir + "scenarios.json"), error);
    REQUIRE(scenarios.has_value());
    const auto* cases = scenarios->member("cases");
    REQUIRE(cases != nullptr);
    REQUIRE(cases->array.size() >= 11);

    const bool update = std::getenv("MAGDA_SDK_UPDATE_GOLDENS") != nullptr;

    for (const auto& c : cases->array) {
        const auto name = c.member("name")->string;
        DYNAMIC_SECTION(name) {
            const auto json = display::toJson(scenario::run(c), {.decimals = 3});
            const auto goldenPath = dir + name + ".json";

            if (update) {
                std::ofstream(goldenPath, std::ios::binary) << json;
                SUCCEED("wrote " << goldenPath);
                continue;
            }

            const auto actual = detail::parseJson(json, error);
            REQUIRE(actual.has_value());
            const auto expected = detail::parseJson(golden::readFile(goldenPath), error);
            INFO(goldenPath << ": " << error);
            REQUIRE(expected.has_value());
            CHECK(golden::firstDifference(*actual, *expected) == "");
        }
    }
}
