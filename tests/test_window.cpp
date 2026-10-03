#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <vector>

#include "magda/sdk/dsp/Window.hpp"

using magda::sdk::fillWindow;
using magda::sdk::Window;
using magda::sdk::WindowType;

namespace {

constexpr WindowType kAll[] = {
    WindowType::rectangular, WindowType::triangular,     WindowType::hann,    WindowType::hamming,
    WindowType::blackman,    WindowType::blackmanHarris, WindowType::flatTop, WindowType::kaiser};

std::vector<float> table(WindowType type, size_t n, bool normalise, float beta = 0.0f) {
    std::vector<float> t(n);
    fillWindow(t.data(), n, type, normalise, beta);
    return t;
}

}  // namespace

TEST_CASE("Hann is the symmetric size - 1 definition", "[window]") {
    const auto t = table(WindowType::hann, 5, false);
    const float expected[] = {0.0F, 0.5F, 1.0F, 0.5F, 0.0F};
    for (size_t i = 0; i < 5; ++i)
        REQUIRE(t[i] == Catch::Approx(expected[i]).margin(1e-6));
}

TEST_CASE("Windows follow their closed forms", "[window]") {
    constexpr size_t n = 257;
    const double denom = static_cast<double>(n - 1);
    auto c = [&](int order, size_t i) {
        return std::cos(order * std::numbers::pi * static_cast<double>(i) / denom);
    };
    const auto hamming = table(WindowType::hamming, n, false);
    const auto blackman = table(WindowType::blackman, n, false);
    const auto harris = table(WindowType::blackmanHarris, n, false);
    const auto flat = table(WindowType::flatTop, n, false);
    const auto tri = table(WindowType::triangular, n, false);
    for (size_t i = 0; i < n; ++i) {
        REQUIRE(hamming[i] == Catch::Approx(0.54 - 0.46 * c(2, i)).margin(1e-5));
        REQUIRE(
            blackman[i] ==
            Catch::Approx(0.5 * (1 - 0.16) - 0.5 * c(2, i) + 0.5 * 0.16 * c(4, i)).margin(1e-5));
        REQUIRE(harris[i] ==
                Catch::Approx(0.35875 - 0.48829 * c(2, i) + 0.14128 * c(4, i) - 0.01168 * c(6, i))
                    .margin(1e-5));
        REQUIRE(flat[i] == Catch::Approx(1.0 - 1.93 * c(2, i) + 1.29 * c(4, i) - 0.388 * c(6, i) +
                                         0.028 * c(8, i))
                               .margin(1e-5));
        REQUIRE(tri[i] ==
                Catch::Approx(1.0 - std::abs((static_cast<double>(i) - denom / 2) / (denom / 2)))
                    .margin(1e-6));
    }
}

TEST_CASE("Every window is symmetric", "[window]") {
    for (auto type : kAll) {
        const auto t = table(type, 128, false, 8.0f);
        for (size_t i = 0; i < t.size() / 2; ++i)
            REQUIRE(t[i] == Catch::Approx(t[t.size() - 1 - i]).margin(1e-5));
    }
}

TEST_CASE("Kaiser peaks at one and narrows with beta", "[window]") {
    const auto wide = table(WindowType::kaiser, 129, false, 2.0f);
    const auto narrow = table(WindowType::kaiser, 129, false, 10.0f);
    REQUIRE(wide[64] == Catch::Approx(1.0).epsilon(1e-5));
    REQUIRE(narrow[64] == Catch::Approx(1.0).epsilon(1e-5));
    REQUIRE(narrow[10] < wide[10]);
}

TEST_CASE("Normalising makes the mean one", "[window]") {
    for (auto type : kAll) {
        if (type == WindowType::flatTop)
            continue;
        const auto t = table(type, 1024, true, 6.0f);
        double sum = 0.0;
        for (float v : t)
            sum += v;
        REQUIRE(sum / 1024.0 == Catch::Approx(1.0).epsilon(1e-5));
    }
}

TEST_CASE("Window applies its table to the leading samples", "[window]") {
    const Window w(8, WindowType::rectangular, false);
    std::vector<float> x(12, 2.0F);
    w.apply(x.data(), 12);
    REQUIRE(x[7] == 2.0F);
    const Window h(4, WindowType::hann, false);
    std::vector<float> y(4, 1.0F);
    h.apply(y.data(), 2);
    REQUIRE(y[0] == 0.0F);
    REQUIRE(y[1] == Catch::Approx(0.75F));
    REQUIRE(y[2] == 1.0F);
}
