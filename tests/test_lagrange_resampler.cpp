#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <vector>

#include "magda/sdk/dsp/LagrangeResampler.hpp"

using magda::sdk::LagrangeResampler;

namespace {

std::vector<float> sine(double cyclesPerSample, int n) {
    std::vector<float> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        x[static_cast<size_t>(i)] =
            static_cast<float>(std::sin(2.0 * std::numbers::pi * cyclesPerSample * i));
    return x;
}

}  // namespace

TEST_CASE("A unit ratio delays the input by two samples", "[lagrange]") {
    const auto x = sine(0.01, 600);
    std::vector<float> y(500);
    LagrangeResampler r;
    REQUIRE(r.process(1.0, x.data(), y.data(), 500) == 500);
    for (size_t i = 10; i < 500; ++i)
        REQUIRE(y[i] == Catch::Approx(x[i - 2]).margin(1e-6));
}

TEST_CASE("A constant stays constant once the history fills", "[lagrange]") {
    const std::vector<float> x(2000, 0.75F);
    std::vector<float> y(900);
    LagrangeResampler r;
    r.process(2000.0 / 900.0, x.data(), y.data(), 900);
    for (size_t i = 5; i < y.size(); ++i)
        REQUIRE(y[i] == Catch::Approx(0.75F).margin(1e-5));
}

TEST_CASE("Downsampling a slow sine keeps its shape", "[lagrange]") {
    constexpr double ratio = 44100.0 / 22050.0;
    const auto x = sine(0.002, 4000);
    std::vector<float> y(1900);
    LagrangeResampler r;
    r.process(ratio, x.data(), y.data(), 1900);
    // Output i reads the input at about i * ratio - 2.
    for (size_t i = 10; i < 1900; ++i)
        REQUIRE(y[i] == Catch::Approx(std::sin(2.0 * std::numbers::pi * 0.002 *
                                               (static_cast<double>(i) * ratio - 2.0)))
                            .margin(1e-3));
}

TEST_CASE("Blocks give the same output as one call", "[lagrange]") {
    const auto x = sine(0.013, 3000);
    constexpr double ratio = 1.3;
    std::vector<float> whole(1000);
    LagrangeResampler a;
    a.process(ratio, x.data(), whole.data(), 1000);

    std::vector<float> blocks(1000);
    LagrangeResampler b;
    const float* in = x.data();
    for (int start = 0; start < 1000; start += 250)
        in += b.process(ratio, in, blocks.data() + start, 250);
    for (size_t i = 0; i < 1000; ++i)
        REQUIRE(blocks[i] == whole[i]);
}

TEST_CASE("reset starts again from silence", "[lagrange]") {
    const auto x = sine(0.02, 400);
    std::vector<float> first(100);
    std::vector<float> second(100);
    LagrangeResampler r;
    r.process(1.0, x.data(), first.data(), 100);
    r.reset();
    r.process(1.0, x.data(), second.data(), 100);
    for (size_t i = 0; i < 100; ++i)
        REQUIRE(first[i] == second[i]);
}
