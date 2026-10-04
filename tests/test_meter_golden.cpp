#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <support/MeterScenario.hpp>

namespace detail = magda::sdk::detail;
namespace display = magda::sdk::display;
namespace scenario = magda::sdk::meter_scenario;

namespace {

const std::string kGoldenDir = std::string(MAGDA_SDK_TESTS_DIR) + "/golden/meter/";

/// Goldens hold three decimals; this absorbs that rounding and libm ulps across platforms.
constexpr double kTolerance = 1.5e-3;

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

double numberOf(const detail::JsonValue& v) {
    return v.type == detail::JsonValue::Type::Int ? static_cast<double>(v.integer) : v.real;
}

bool isNumber(const detail::JsonValue& v) {
    return v.type == detail::JsonValue::Type::Int || v.type == detail::JsonValue::Type::Double;
}

/// Empty when equal, else the path of the first difference.
std::string firstDifference(const detail::JsonValue& a, const detail::JsonValue& b,
                            const std::string& path) {
    if (isNumber(a) && isNumber(b))
        return std::abs(numberOf(a) - numberOf(b)) <= kTolerance ? "" : path;
    if (a.type != b.type)
        return path;
    switch (a.type) {
        case detail::JsonValue::Type::Array:
            if (a.array.size() != b.array.size())
                return path + " (length)";
            for (std::size_t i = 0; i < a.array.size(); ++i)
                if (auto d = firstDifference(a.array[i], b.array[i],
                                             path + "[" + std::to_string(i) + "]");
                    !d.empty())
                    return d;
            return "";
        case detail::JsonValue::Type::Object:
            if (a.object.size() != b.object.size())
                return path + " (members)";
            for (std::size_t i = 0; i < a.object.size(); ++i) {
                if (a.object[i].first != b.object[i].first)
                    return path + "." + a.object[i].first;
                if (auto d = firstDifference(a.object[i].second, b.object[i].second,
                                             path + "." + a.object[i].first);
                    !d.empty())
                    return d;
            }
            return "";
        case detail::JsonValue::Type::String:
            return a.string == b.string ? "" : path;
        case detail::JsonValue::Type::Bool:
            return a.boolean == b.boolean ? "" : path;
        default:
            return "";
    }
}

}  // namespace

TEST_CASE("Meter frames match their golden display lists", "[meter][golden]") {
    std::string error;
    const auto scenarios = detail::parseJson(readFile(kGoldenDir + "scenarios.json"), error);
    REQUIRE(scenarios.has_value());
    const auto cases = scenario::casesOf(*scenarios);
    REQUIRE(cases.size() >= 6);

    const bool update = std::getenv("MAGDA_SDK_UPDATE_GOLDENS") != nullptr;

    for (const auto& c : cases) {
        DYNAMIC_SECTION(c.name) {
            magda::sdk::MeterModel meter;
            const auto json = display::toJson(scenario::run(c, meter), {.decimals = 3});
            const auto goldenPath = kGoldenDir + c.name + ".json";

            if (update) {
                std::ofstream(goldenPath, std::ios::binary) << json;
                SUCCEED("wrote " << goldenPath);
                continue;
            }

            const auto actual = detail::parseJson(json, error);
            REQUIRE(actual.has_value());
            const auto golden = detail::parseJson(readFile(goldenPath), error);
            INFO(goldenPath << ": " << error);
            REQUIRE(golden.has_value());
            INFO(json);
            CHECK(firstDifference(*actual, *golden, "$") == "");
        }
    }
}
