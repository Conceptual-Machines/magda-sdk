#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include "magda/sdk/dsp/Fft.hpp"

using magda::sdk::RealFft;

namespace {

std::vector<float> testSignal(int n, unsigned seed) {
    std::vector<float> x(static_cast<size_t>(n));
    unsigned state = seed;
    for (auto& v : x) {
        state = state * 1664525U + 1013904223U;
        v = static_cast<float>(state >> 8) / static_cast<float>(1U << 23) - 1.0F;
    }
    return x;
}

std::vector<std::complex<double>> naiveDft(const std::vector<float>& x) {
    const auto n = x.size();
    std::vector<std::complex<double>> out(n / 2 + 1);
    for (size_t k = 0; k < out.size(); ++k) {
        std::complex<double> acc = 0.0;
        for (size_t i = 0; i < n; ++i)
            acc += static_cast<double>(x[i]) *
                   std::polar(1.0, -2.0 * std::numbers::pi * static_cast<double>(k * i) /
                                       static_cast<double>(n));
        out[k] = acc;
    }
    return out;
}

}  // namespace

TEST_CASE("RealFft rejects sizes it cannot do", "[fft]") {
    RealFft fft;
    for (int bad : {-32, 0, 1, 16, 33, 48, 1000, RealFft::kMaxSize * 2})
        REQUIRE_FALSE(fft.prepare(bad));
    REQUIRE_FALSE(fft.isPrepared());
    REQUIRE(fft.prepare(RealFft::kMinSize));
    REQUIRE(fft.isPrepared());
    REQUIRE_FALSE(fft.prepare(100));
    REQUIRE_FALSE(fft.isPrepared());
}

TEST_CASE("RealFft forward is the unscaled DFT", "[fft]") {
    for (int n : {32, 64, 256, 1024, 2048}) {
        RealFft fft;
        REQUIRE(fft.prepare(n));
        REQUIRE(fft.size() == n);
        REQUIRE(fft.spectrumValues() == n + 2);

        const auto x = testSignal(n, static_cast<unsigned>(n));
        std::vector<float> spectrum(static_cast<size_t>(fft.spectrumValues()));
        fft.forward(x.data(), spectrum.data());

        const auto expected = naiveDft(x);
        double peak = 0.0;
        for (const auto& bin : expected)
            peak = std::max(peak, std::abs(bin));
        for (size_t k = 0; k < expected.size(); ++k) {
            INFO("n " << n << " bin " << k);
            REQUIRE(spectrum[2 * k] == Catch::Approx(expected[k].real()).margin(1e-5 * peak));
            REQUIRE(spectrum[2 * k + 1] == Catch::Approx(expected[k].imag()).margin(1e-5 * peak));
        }
        REQUIRE(spectrum[1] == 0.0F);
        REQUIRE(spectrum[static_cast<size_t>(n) + 1] == 0.0F);
    }
}

TEST_CASE("RealFft magnitude matches the forward spectrum", "[fft]") {
    RealFft fft;
    REQUIRE(fft.prepare(512));
    const auto x = testSignal(512, 5);
    std::vector<float> spectrum(514);
    std::vector<float> magnitude(257);
    fft.forward(x.data(), spectrum.data());
    fft.forwardMagnitude(x.data(), magnitude.data());
    for (size_t k = 0; k < magnitude.size(); ++k)
        REQUIRE(magnitude[k] ==
                Catch::Approx(std::hypot(spectrum[2 * k], spectrum[2 * k + 1])).margin(1e-6));
}

TEST_CASE("RealFft inverse undoes forward", "[fft]") {
    for (int n : {32, 128, 2048}) {
        RealFft fft;
        REQUIRE(fft.prepare(n));
        const auto x = testSignal(n, 21);
        std::vector<float> spectrum(static_cast<size_t>(fft.spectrumValues()));
        std::vector<float> back(static_cast<size_t>(n));
        fft.forward(x.data(), spectrum.data());
        fft.inverse(spectrum.data(), back.data());
        for (int i = 0; i < n; ++i)
            REQUIRE(back[static_cast<size_t>(i)] ==
                    Catch::Approx(x[static_cast<size_t>(i)]).margin(1e-6));
    }
}

TEST_CASE("RealFft places a sine in its bin", "[fft]") {
    constexpr int n = 1024;
    constexpr int bin = 37;
    RealFft fft;
    REQUIRE(fft.prepare(n));
    std::vector<float> x(n);
    for (int i = 0; i < n; ++i)
        x[static_cast<size_t>(i)] =
            static_cast<float>(std::sin(2.0 * std::numbers::pi * bin * i / n));
    std::vector<float> magnitude(n / 2 + 1);
    fft.forwardMagnitude(x.data(), magnitude.data());
    REQUIRE(magnitude[bin] == Catch::Approx(n / 2.0).epsilon(1e-5));
    REQUIRE(magnitude[bin + 5] < 1e-3F);
}

TEST_CASE("RealFft survives a move", "[fft]") {
    RealFft a;
    REQUIRE(a.prepare(64));
    RealFft b = std::move(a);
    REQUIRE_FALSE(a.isPrepared());
    REQUIRE(b.isPrepared());

    std::vector<float> x(64, 0.0F);
    x[0] = 1.0F;
    std::vector<float> magnitude(33);
    b.forwardMagnitude(x.data(), magnitude.data());
    for (float m : magnitude)
        REQUIRE(m == Catch::Approx(1.0F).epsilon(1e-6));
}
