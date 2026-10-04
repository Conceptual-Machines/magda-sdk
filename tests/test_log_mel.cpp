#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <vector>

#include "magda/sdk/analysis/LogMel.hpp"

using Catch::Approx;
using magda::sdk::LogMelConfig;
using magda::sdk::LogMelFrontEnd;
using magda::sdk::MelCompression;
using magda::sdk::MelPadding;
using magda::sdk::MelScale;
using magda::sdk::MelSpectrum;

namespace {

std::vector<float> sine(double hz, double rate, int n) {
    std::vector<float> s(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        s[static_cast<std::size_t>(i)] =
            static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * hz * i / rate));
    return s;
}

/// Centre frequency of mel band @p m, from where its filter peaks.
int peakBin(const std::vector<float>& filterbank, int bins, int m) {
    const auto row = filterbank.begin() + static_cast<std::ptrdiff_t>(m) * bins;
    return static_cast<int>(std::max_element(row, row + bins) - row);
}

}  // namespace

TEST_CASE("Mel filterbanks are triangles between their edges", "[logmel]") {
    LogMelConfig config;
    const auto fb = magda::sdk::melFilterbank(config);
    const int bins = config.fftSize / 2 + 1;
    REQUIRE(fb.size() == static_cast<std::size_t>(config.numMels * bins));
    CHECK(*std::max_element(fb.begin(), fb.end()) <= 1.0f);
    CHECK(*std::min_element(fb.begin(), fb.end()) >= 0.0f);
    for (int m = 1; m < config.numMels; ++m)
        CHECK(peakBin(fb, bins, m) >= peakBin(fb, bins, m - 1));
    // Nothing below fMin or above fMax.
    CHECK(fb[0] == 0.0f);
    CHECK(fb[static_cast<std::size_t>(config.numMels * bins - 1)] == 0.0f);
}

TEST_CASE("The Slaney scale is linear below 1 kHz", "[logmel]") {
    LogMelConfig config;
    config.scale = MelScale::Slaney;
    config.sampleRate = 22050.0;
    config.fMin = 0.0;
    config.fMax = 2000.0;
    config.numMels = 29;
    const auto fb = magda::sdk::melFilterbank(config);
    const int bins = config.fftSize / 2 + 1;
    const double binHz = config.sampleRate / config.fftSize;
    // Band 0 peaks at the first interior edge, melMax / 30 mels, which is in the linear part.
    const double melMax = 15.0 + std::log(2.0) / (std::log(6.4) / 27.0);
    const double centreHz = (melMax / 30.0) * (200.0 / 3.0);
    CHECK(std::abs(peakBin(fb, bins, 0) * binHz - centreHz) <= binHz);
}

TEST_CASE("Frames follow the centred STFT count", "[logmel]") {
    LogMelFrontEnd zero;
    LogMelConfig config;
    REQUIRE(zero.prepare(config));
    CHECK(zero.numFrames(480000) == 1001);
    CHECK(zero.numFrames(0) == 0);

    config.padding = MelPadding::Reflect;
    LogMelFrontEnd reflect;
    REQUIRE(reflect.prepare(config));
    CHECK(reflect.numFrames(1024) == 0);
    CHECK(reflect.numFrames(1025) == 1025 / 480 + 1);
}

TEST_CASE("A sine lights the band that holds it", "[logmel]") {
    LogMelConfig config;
    config.numMels = 32;
    LogMelFrontEnd frontEnd;
    REQUIRE(frontEnd.prepare(config));
    const auto mel = frontEnd.compute(sine(1000.0, 48000.0, 48000).data(), 48000);
    REQUIRE(mel.size() == static_cast<std::size_t>(frontEnd.numFrames(48000) * 32));

    const auto fb = magda::sdk::melFilterbank(config);
    const int bins = config.fftSize / 2 + 1;
    const int frame = 50;
    const auto row = mel.begin() + frame * 32;
    const int loudest = static_cast<int>(std::max_element(row, row + 32) - row);
    const double centreHz = peakBin(fb, bins, loudest) * 48000.0 / config.fftSize;
    CHECK(std::abs(centreHz - 1000.0) < 150.0);
}

TEST_CASE("Silence reads as the log floor", "[logmel]") {
    std::vector<float> silence(4800, 0.0f);

    LogMelConfig config;
    LogMelFrontEnd log;
    REQUIRE(log.prepare(config));
    const auto a = log.compute(silence.data(), 4800);
    CHECK(a.front() == Approx(std::log(1e-10)));

    config.compression = MelCompression::Log1p;
    config.log1pScale = 1000.0;
    LogMelFrontEnd log1p;
    REQUIRE(log1p.prepare(config));
    CHECK(log1p.compute(silence.data(), 4800).front() == Approx(std::log1p(1000.0 * 1e-10)));
}

TEST_CASE("Magnitude and power spectra agree on where the energy is", "[logmel]") {
    const auto tone = sine(440.0, 22050.0, 22050);
    LogMelConfig config;
    config.sampleRate = 22050.0;
    config.hopSize = 441;
    config.numMels = 40;
    config.fMax = 8000.0;

    LogMelFrontEnd power;
    REQUIRE(power.prepare(config));
    config.spectrum = MelSpectrum::Magnitude;
    config.padding = MelPadding::Reflect;
    LogMelFrontEnd magnitude;
    REQUIRE(magnitude.prepare(config));

    const auto p = power.compute(tone.data(), 22050);
    const auto m = magnitude.compute(tone.data(), 22050);
    const int frame = 20;
    const auto argmax = [&](const std::vector<float>& v) {
        const auto row = v.begin() + frame * 40;
        return std::max_element(row, row + 40) - row;
    };
    CHECK(argmax(p) == argmax(m));
}
