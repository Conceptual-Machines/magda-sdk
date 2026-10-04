#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <support/CurveEditorScenario.hpp>
#include <support/GoldenJson.hpp>

namespace detail = magda::sdk::detail;
namespace display = magda::sdk::display;
namespace golden = magda::sdk::golden_json;
namespace scenario = magda::sdk::curve_editor_scenario;

TEST_CASE("Curve editor frames match their golden display lists", "[curveedit][golden]") {
    const std::string dir = std::string(MAGDA_SDK_TESTS_DIR) + "/golden/curve-editor/";
    std::string error;
    const auto scenarios = detail::parseJson(golden::readFile(dir + "scenarios.json"), error);
    REQUIRE(scenarios.has_value());
    const auto* cases = scenarios->member("cases");
    REQUIRE(cases != nullptr);
    REQUIRE(cases->array.size() >= 16);

    const bool update = std::getenv("MAGDA_SDK_UPDATE_GOLDENS") != nullptr;

    for (const auto& c : cases->array) {
        const auto name = c.member("name")->string;
        DYNAMIC_SECTION(name) {
            magda::sdk::CurveEditor editor;
            const auto json = display::toJson(scenario::run(c, editor), {.decimals = 3});
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
            INFO(json);
            CHECK(golden::firstDifference(*actual, *expected) == "");
        }
    }
}
