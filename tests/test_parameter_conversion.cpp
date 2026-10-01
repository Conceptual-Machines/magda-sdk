#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "magda/sdk/device/ParameterDescriptor.hpp"

using namespace magda::sdk;
using Catch::Approx;

namespace {

ParameterDomain domain(ParameterScale scale, float min, float max) {
    ParameterDomain d;
    d.scale = scale;
    d.minValue = min;
    d.maxValue = max;
    return d;
}

}  // namespace

TEST_CASE("Linear maps the ends and the middle", "[parameter]") {
    const auto d = domain(ParameterScale::Linear, 10.0f, 20.0f);
    CHECK(normalizedToReal(0.0f, d) == 10.0f);
    CHECK(normalizedToReal(0.5f, d) == 15.0f);
    CHECK(normalizedToReal(1.0f, d) == 20.0f);
    CHECK(normalizedToReal(-3.0f, d) == 10.0f);
    CHECK(normalizedToReal(7.0f, d) == 20.0f);
    CHECK(realToNormalized(15.0f, d) == 0.5f);
    CHECK(realToNormalized(99.0f, d) == 1.0f);
}

TEST_CASE("A linear anchor lands at the middle of the slider", "[parameter]") {
    auto d = domain(ParameterScale::Linear, 0.0f, 100.0f);
    d.scaleAnchor = 25.0f;
    CHECK(normalizedToReal(0.5f, d) == Approx(25.0f).margin(1e-3));
    CHECK(realToNormalized(25.0f, d) == Approx(0.5f).margin(1e-4));

    d.scaleAnchor = 100.0f;
    CHECK_FALSE(hasScaleAnchor(d));
    CHECK(normalizedToReal(0.5f, d) == 50.0f);
}

TEST_CASE("Logarithmic is geometric, with and without an anchor", "[parameter]") {
    auto d = domain(ParameterScale::Logarithmic, 20.0f, 20000.0f);
    CHECK(normalizedToReal(0.5f, d) == Approx(632.455f).epsilon(1e-4));
    CHECK(realToNormalized(632.455f, d) == Approx(0.5f).margin(1e-4));

    d.scaleAnchor = 1000.0f;
    CHECK(normalizedToReal(0.5f, d) == Approx(1000.0f).epsilon(1e-3));
}

TEST_CASE("Logarithmic with a non-positive minimum reads linearly", "[parameter]") {
    const auto d = domain(ParameterScale::Logarithmic, 0.0f, 10.0f);
    CHECK(normalizedToReal(0.5f, d) == 5.0f);
    CHECK(realToNormalized(5.0f, d) == 0.5f);
}

TEST_CASE("Exponential raises the position to the exponent", "[parameter]") {
    auto d = domain(ParameterScale::Exponential, -12.0f, 6.0f);
    d.exponent = 2.0f;
    CHECK(normalizedToReal(0.5f, d) == Approx(-7.5f));
    CHECK(realToNormalized(-7.5f, d) == Approx(0.5f));
    d.exponent = 0.0f;
    CHECK(realToNormalized(0.0f, d) == 0.0f);
}

TEST_CASE("Discrete and Boolean step", "[parameter]") {
    auto d = domain(ParameterScale::Discrete, 0.0f, 3.0f);
    d.choiceCount = 4;
    CHECK(isStepped(d));
    CHECK(normalizedToReal(0.4f, d) == 1.0f);
    CHECK(realToNormalized(2.0f, d) == Approx(2.0f / 3.0f));
    CHECK(realToNormalized(9.0f, d) == 1.0f);
    d.choiceCount = 0;
    CHECK(normalizedToReal(0.9f, d) == 0.0f);

    const auto b = domain(ParameterScale::Boolean, 0.0f, 1.0f);
    CHECK(normalizedToReal(0.49f, b) == 0.0f);
    CHECK(normalizedToReal(0.5f, b) == 1.0f);
    CHECK(realToNormalized(0.5f, b) == 1.0f);
}

TEST_CASE("FaderDB puts unity at three quarters", "[parameter]") {
    const auto d = domain(ParameterScale::FaderDB, -60.0f, 6.0f);
    CHECK(normalizedToReal(0.0f, d) == -60.0f);
    CHECK(normalizedToReal(0.75f, d) == 0.0f);
    CHECK(normalizedToReal(1.0f, d) == 6.0f);
    CHECK(realToNormalized(0.0f, d) == 0.75f);
    CHECK(realToNormalized(-30.0f, d) == Approx(0.375f));
}

TEST_CASE("FaderDB takes its unity point from the domain", "[parameter]") {
    auto d = domain(ParameterScale::FaderDB, -60.0f, 12.0f);
    d.unityPosition = 0.5f;
    d.unityDb = -6.0f;
    CHECK(normalizedToReal(0.5f, d) == -6.0f);
    CHECK(realToNormalized(-6.0f, d) == 0.5f);
}

TEST_CASE("Conversions invert each other across the continuous kinds", "[parameter]") {
    auto linear = domain(ParameterScale::Linear, -24.0f, 24.0f);
    linear.scaleAnchor = -6.0f;
    auto log = domain(ParameterScale::Logarithmic, 0.1f, 10000.0f);
    log.scaleAnchor = 800.0f;
    auto expo = domain(ParameterScale::Exponential, 0.0f, 5.0f);
    expo.exponent = 3.0f;
    const auto fader = domain(ParameterScale::FaderDB, -60.0f, 6.0f);

    for (const auto& d : {linear, log, expo, fader})
        for (float n = 0.0f; n <= 1.0f; n += 0.05f)
            CHECK(realToNormalized(normalizedToReal(n, d), d) == Approx(n).margin(2e-4));
}

TEST_CASE("Choices from labels keep order and keep repeated labels apart", "[parameter]") {
    const auto choices = choicesFromLabels({"Off", "On", "On"});
    REQUIRE(choices.size() == 3);
    CHECK(choices[0].id == "Off");
    CHECK(choices[1].id == "On_1");
    CHECK(choices[2].id == "On_2");
    CHECK(choices[2].value == 2.0f);
}

TEST_CASE("A descriptor resolves its slot and its derived id", "[parameter]") {
    ParameterDescriptor unset;
    const auto derived = resolveDescriptor(unset, "magda_delay", 4);
    CHECK(derived.index == 4);
    CHECK(derived.stableId == "magda_delay_param_4");

    ParameterDescriptor set;
    set.stableId = "time";
    set.index = 9;
    const auto kept = resolveDescriptor(set, "magda_delay", 4);
    CHECK(kept.index == 9);
    CHECK(kept.stableId == "time");

    ParameterDescriptor indexOnly;
    indexOnly.index = 7;
    CHECK(resolveDescriptor(indexOnly, "x", 0).stableId == "x_param_7");
}
