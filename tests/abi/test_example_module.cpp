#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "magda/sdk/abi/AbiDevice.hpp"
#include "magda/sdk/abi/AbiHarness.hpp"

TEST_CASE("The example gain module conforms and applies its gain", "[abi][example]") {
    const auto* module = magda::sdk::host::linkedModule();
    const auto failures = magda::sdk::host::checkModuleConformance(module);
    for (const auto& failure : failures)
        UNSCOPED_INFO(failure);
    CHECK(failures.empty());

    magda::sdk::host::AbiDevice gain(*module, "exampleGain");
    REQUIRE(gain.prepare(48000.0, 32) == MAGDA_OK);
    const float minus6 = gain.api().param_to_normalized(gain.get(), 0, -6.0f);
    REQUIRE(gain.api().set_param(gain.get(), 0, minus6) == MAGDA_OK);
    std::vector<float> left(32, 1.0f), right(32, 1.0f);
    std::array<float*, 2> channels{left.data(), right.data()};
    REQUIRE(gain.process(channels.data(), 2, 32) == MAGDA_OK);
    CHECK(left[31] > 0.50f);
    CHECK(left[31] < 0.51f);
}
