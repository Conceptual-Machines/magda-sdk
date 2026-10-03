#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include "magda/sdk/dsp/Oversampler.hpp"

using magda::sdk::Oversampler;

namespace {

/// Amplitude of the component at @p cyclesPerSample over [begin, end).
double amplitudeAt(const std::vector<float>& x, double cyclesPerSample, std::size_t begin,
                   std::size_t end) {
    std::complex<double> acc = 0.0;
    for (std::size_t n = begin; n < end; ++n)
        acc += static_cast<double>(x[n]) *
               std::polar(1.0, -2.0 * std::numbers::pi * cyclesPerSample * static_cast<double>(n));
    return 2.0 * std::abs(acc) / static_cast<double>(end - begin);
}

std::vector<float> sine(double cyclesPerSample, std::size_t n) {
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * cyclesPerSample * i));
    return x;
}

double db(double linear) {
    return 20.0 * std::log10(linear);
}

constexpr std::size_t kBlock = 4096;
constexpr int kBlockInt = static_cast<int>(kBlock);

}  // namespace

TEST_CASE("Upsampler passes the base band at unity", "[dsp][oversampler]") {
    for (const int factor : {2, 4}) {
        Oversampler os;
        os.prepare(factor);
        for (const double f : {0.02, 0.1, 0.2}) {
            os.reset();
            const auto in = sine(f, kBlock);
            std::vector<float> out(kBlock * static_cast<std::size_t>(factor));
            os.upsample(in.data(), kBlockInt, out.data());
            const double gain = amplitudeAt(out, f / factor, out.size() / 4, out.size());
            INFO("factor " << factor << " f " << f);
            REQUIRE(std::abs(db(gain)) < 0.25);
        }
    }
}

TEST_CASE("Upsampler rejects the images", "[dsp][oversampler]") {
    for (const int factor : {2, 4}) {
        Oversampler os;
        os.prepare(factor);
        const double f = 0.1;
        const auto in = sine(f, kBlock);
        std::vector<float> out(kBlock * static_cast<std::size_t>(factor));
        os.upsample(in.data(), kBlockInt, out.data());
        const double passband = amplitudeAt(out, f / factor, out.size() / 4, out.size());
        // The first image sits at fs - f in base-rate cycles per sample.
        const double image = amplitudeAt(out, (1.0 - f) / factor, out.size() / 4, out.size());
        INFO("factor " << factor);
        REQUIRE(db(image / passband) < -35.0);
    }
}

TEST_CASE("Downsampler rejects what the base rate cannot hold", "[dsp][oversampler]") {
    for (const int factor : {2, 4}) {
        Oversampler os;
        os.prepare(factor);
        // 0.75 of the base rate in oversampled cycles per sample: above the cutoff, and it
        // aliases to 0.25 of the base rate if the decimator lets it through.
        const double f = 0.75 / factor;
        const auto in = sine(f, kBlock * static_cast<std::size_t>(factor));
        std::vector<float> out(kBlock);
        os.downsample(in.data(), kBlockInt, out.data());
        const double alias = amplitudeAt(out, 0.25, kBlock / 4, kBlock);
        INFO("factor " << factor);
        REQUIRE(db(alias) < -30.0);
    }
}

TEST_CASE("Round trip keeps unity gain and reports its latency", "[dsp][oversampler]") {
    for (const int factor : {2, 4}) {
        Oversampler os;
        os.prepare(factor);
        INFO("factor " << factor);

        REQUIRE(os.latencySamples() == 48 / factor - 1);

        std::vector<float> in(kBlock, 0.0f), up(kBlock * static_cast<std::size_t>(factor)),
            out(kBlock);
        in[100] = 1.0f;
        os.upsample(in.data(), kBlockInt, up.data());
        os.downsample(up.data(), kBlockInt, out.data());

        std::size_t peak = 0;
        for (std::size_t i = 0; i < out.size(); ++i)
            if (std::abs(out[i]) > std::abs(out[peak]))
                peak = i;
        REQUIRE(peak == static_cast<std::size_t>(100 + os.latencySamples()));

        os.reset();
        const auto tone = sine(0.05, kBlock);
        os.upsample(tone.data(), kBlockInt, up.data());
        os.downsample(up.data(), kBlockInt, out.data());
        REQUIRE(std::abs(db(amplitudeAt(out, 0.05, kBlock / 4, kBlock))) < 0.25);
    }
}

TEST_CASE("Oversampler streams across block boundaries", "[dsp][oversampler]") {
    Oversampler whole, split;
    whole.prepare(4);
    split.prepare(4);
    const auto in = sine(0.07, 256);
    std::vector<float> a(1024), b(1024);
    whole.upsample(in.data(), 256, a.data());
    split.upsample(in.data(), 100, b.data());
    split.upsample(in.data() + 100, 156, b.data() + 400);
    REQUIRE(a == b);
}
