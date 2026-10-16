#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <vector>

#include "magda/sdk/analysis/Spectrogram.hpp"

using Catch::Approx;
using magda::sdk::Spectrogram;
using magda::sdk::SpectrogramConfig;

namespace {

std::vector<float> sine(double hz, double rate, int n, double amplitude = 1.0) {
    std::vector<float> s(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        s[static_cast<std::size_t>(i)] =
            static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz * i / rate));
    return s;
}

int rowOf(double hz, const SpectrogramConfig& c) {
    return static_cast<int>(c.numRows * std::log(hz / c.lowHz) / std::log(c.highHz / c.lowHz));
}

}  // namespace

TEST_CASE("A full-scale sine peaks near 0 dB in its own row", "[spectrogram]") {
    SpectrogramConfig config;
    Spectrogram spec;
    REQUIRE(spec.prepare(config));

    const auto s = sine(1000.0, config.sampleRate, 4800);
    std::vector<float> rows(static_cast<std::size_t>(spec.numRows()));
    spec.column(s.data(), static_cast<int>(s.size()), 2400.0, rows.data());

    const auto loudest = std::max_element(rows.begin(), rows.end());
    CHECK(std::abs(static_cast<int>(loudest - rows.begin()) - rowOf(1000.0, config)) <= 1);
    CHECK(*loudest == Approx(0.0f).margin(1.0f));
    CHECK(rows.front() < *loudest - 40.0f);
}

TEST_CASE("Halving the amplitude lowers the level by 6 dB", "[spectrogram]") {
    SpectrogramConfig config;
    Spectrogram spec;
    REQUIRE(spec.prepare(config));

    std::vector<float> full(static_cast<std::size_t>(spec.numRows()));
    std::vector<float> half(full.size());
    const auto a = sine(2000.0, config.sampleRate, 4800, 1.0);
    const auto b = sine(2000.0, config.sampleRate, 4800, 0.5);
    spec.column(a.data(), 4800, 2400.0, full.data());
    spec.column(b.data(), 4800, 2400.0, half.data());
    const int r = rowOf(2000.0, config);
    CHECK(full[static_cast<std::size_t>(r)] - half[static_cast<std::size_t>(r)] ==
          Approx(6.02f).margin(0.05f));
}

TEST_CASE("An impulse shows only in windows that reach it", "[spectrogram]") {
    SpectrogramConfig config;
    Spectrogram spec;
    REQUIRE(spec.prepare(config));

    std::vector<float> s(4800, 0.0f);
    s[2000] = 1.0f;
    std::vector<float> rows(static_cast<std::size_t>(spec.numRows()));

    spec.column(s.data(), 4800, 2000.0, rows.data());
    CHECK(*std::min_element(rows.begin(), rows.end()) > -60.0f);

    spec.column(s.data(), 4800, 2000.0 + spec.windowLength(), rows.data());
    CHECK(*std::max_element(rows.begin(), rows.end()) < -150.0f);
}

TEST_CASE("Windows past either end read the signal as zero", "[spectrogram]") {
    SpectrogramConfig config;
    Spectrogram spec;
    REQUIRE(spec.prepare(config));

    const auto s = sine(500.0, config.sampleRate, 1000);
    std::vector<float> rows(static_cast<std::size_t>(spec.numRows()));
    spec.column(s.data(), 1000, -1000.0, rows.data());
    CHECK(*std::max_element(rows.begin(), rows.end()) < -150.0f);
    spec.column(s.data(), 1000, 0.0, rows.data());
    CHECK(*std::max_element(rows.begin(), rows.end()) > -20.0f);
}

TEST_CASE("Spectrogram rejects configs it cannot run", "[spectrogram]") {
    Spectrogram spec;
    SpectrogramConfig config;

    config.fftSize = 1000;
    CHECK_FALSE(spec.prepare(config));

    config = {};
    config.windowSeconds = 0.1;  // 4800 samples, longer than the transform
    CHECK_FALSE(spec.prepare(config));

    config = {};
    config.highHz = config.lowHz;
    CHECK_FALSE(spec.prepare(config));

    config = {};
    config.numRows = 0;
    CHECK_FALSE(spec.prepare(config));
}
