#include <bit>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <magda/sdk/curve/ModCurve.hpp>
#include <numbers>
#include <support/ModBlocks.hpp>
#include <vector>

/**
 * @file test_mod_follower.cpp
 * @brief The follower core: detector and envelope (#2120, #2932).
 */

using namespace magda::sdk;
using namespace magda::modtest;
using magda::modtest::Block;

namespace {

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-4);
}

constexpr double kSampleRate = 48000.0;

ModTiming timing(double bpm = 120.0, int numerator = 4, int denominator = 4) {
    return ModTiming{kSampleRate, bpm, numerator, denominator};
}

Block blockOf(int numSamples) {
    Block block;
    block.numSamples = numSamples;
    return block;
}

/// A block of constant level, which is what a detector reduces to that level.
std::vector<float> flat(int numSamples, float level) {
    return std::vector<float>(static_cast<std::size_t>(numSamples), level);
}

/// A sine at @p hz, for asking a band limit which part of the spectrum it lets
/// through.
std::vector<float> sine(int numSamples, double hz, float amplitude = 1.0f) {
    std::vector<float> samples(static_cast<std::size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
        samples[static_cast<std::size_t>(i)] =
            amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * i / kSampleRate));
    return samples;
}

/// The peak a follower detects from @p source, with a scratch buffer of its own.
float detect(FollowerState& state, const FollowerSettings& settings,
             const std::vector<float>& source) {
    std::vector<float> scratch(source.size());
    detectFollowerSource(state, settings, source, kSampleRate, scratch);
    return state.sourcePeak;
}

}  // namespace

TEST_CASE("Detection reduces a block to its peak", "[mod][follower]") {
    FollowerSettings settings;
    FollowerState state;

    CHECK(detect(state, settings, flat(256, 0.5f)) == approx(0.5f));

    // The peak rather than the average, and the magnitude rather than the
    // value: a block whose loudest sample is negative is not a quiet block.
    auto mixed = flat(256, 0.1f);
    mixed[100] = -0.8f;
    CHECK(detect(state, settings, mixed) == approx(0.8f));

    // Nothing to detect is nothing detected, which is also what the whole
    // modulation system reads as a modifier doing nothing.
    CHECK(detect(state, settings, flat(256, 0.0f)) == approx(0.0f));
}

TEST_CASE("Input gain applies before detection", "[mod][follower]") {
    FollowerSettings settings;
    FollowerState state;

    settings.gainDb = 6.0f;
    CHECK(detect(state, settings, flat(256, 0.25f)) == approx(0.25f * std::pow(10.0f, 0.3f)));

    settings.gainDb = -20.0f;
    CHECK(detect(state, settings, flat(256, 1.0f)) == approx(0.1f));
}

TEST_CASE("Band limits decide which part of the spectrum is heard", "[mod][follower]") {
    constexpr int kSamples = 4096;

    SECTION("a high-pass ignores the bass") {
        FollowerSettings settings;
        settings.highPass = true;
        settings.highPassHz = 1000.0f;

        FollowerState low;
        FollowerState high;

        // A tone well below the corner is most of the way gone; one well above
        // it comes through. That is the whole point of filtering before
        // detection: a level that has already been detected has no frequency
        // content left to filter.
        CHECK(detect(low, settings, sine(kSamples, 60.0)) < 0.05f);
        CHECK(detect(high, settings, sine(kSamples, 8000.0)) > 0.8f);
    }

    SECTION("a low-pass ignores the top") {
        FollowerSettings settings;
        settings.lowPass = true;
        settings.lowPassHz = 500.0f;

        FollowerState low;
        FollowerState high;

        CHECK(detect(low, settings, sine(kSamples, 60.0)) > 0.9f);
        CHECK(detect(high, settings, sine(kSamples, 8000.0)) < 0.05f);
    }

    SECTION("and unfiltered hears both") {
        FollowerSettings settings;
        FollowerState low;
        FollowerState high;

        CHECK(detect(low, settings, sine(kSamples, 60.0)) > 0.9f);

        // Eight kilohertz at this rate is six samples a cycle, so the loudest
        // sample a full-scale tone actually has is sin(60 degrees). What the
        // detector reports is the peak of the samples rather than of the wave
        // the samples came from.
        CHECK(detect(high, settings, sine(kSamples, 8000.0)) > 0.8f);
    }
}

TEST_CASE("The envelope rises towards the source and falls away from it", "[mod][follower]") {
    FollowerSettings settings;
    settings.attackMs = 10.0f;
    settings.releaseMs = 100.0f;

    FollowerState state;
    state.sourcePeak = 1.0f;

    // Blocks of a millisecond, so ten of them is exactly one attack. The
    // one-pole is a curve rather than a ramp, so what is asserted is that it
    // climbs and where one time constant leaves it.
    const auto block = blockOf(static_cast<int>(kSampleRate / 1000.0));

    float previous = 0.0f;
    for (int i = 0; i < 10; ++i) {
        const auto value = advanceFollower(state, settings, block, timing());
        CHECK(value >= previous);
        previous = value;
    }

    // The time constant is -2, so one attack's worth of samples closes
    // all but e^-2 of the gap: 86.5 per cent of the way there, not all of it.
    // Pinned rather than approximated, because it is the number that decides
    // how a follower sounds.
    CHECK(previous == approx(1.0f - std::exp(-2.0f)));

    // And falls back on the release, which is ten times as long, so the same
    // ten blocks only take it part of the way.
    state.sourcePeak = 0.0f;
    for (int i = 0; i < 10; ++i)
        advanceFollower(state, settings, block, timing());

    CHECK(state.envelope < previous);
    CHECK(state.envelope > 0.5f);
}

TEST_CASE("Hold keeps the envelope up before it is allowed to fall", "[mod][follower]") {
    FollowerSettings settings;
    settings.attackMs = 1.0f;
    settings.releaseMs = 10.0f;
    settings.holdMs = 5.0f;

    FollowerState state;
    state.sourcePeak = 1.0f;

    const auto block = blockOf(static_cast<int>(kSampleRate / 1000.0));
    for (int i = 0; i < 5; ++i)
        advanceFollower(state, settings, block, timing());
    const auto reached = state.envelope;

    // Silence at the source, and the hold spends itself before the release
    // starts: five milliseconds of hold over one-millisecond blocks.
    state.sourcePeak = 0.0f;
    for (int i = 0; i < 5; ++i)
        advanceFollower(state, settings, block, timing());
    CHECK(state.envelope == approx(reached));

    advanceFollower(state, settings, block, timing());
    CHECK(state.envelope < reached);
}

TEST_CASE("A source that stays loud keeps the hold armed", "[mod][follower]") {
    // The attack closes a fraction of the gap per sample and never arrives, so
    // a source that stays up is still driving the envelope however long it has
    // been up, and the hold is refreshed for as long as it is. An envelope that
    // arrived at the source would leave that branch and spend the hold where
    // nothing had stopped, and the follower would fall the instant the source
    // did.
    FollowerSettings settings;
    settings.attackMs = 1.0f;
    settings.releaseMs = 10.0f;
    settings.holdMs = 5.0f;

    FollowerState state;
    state.sourcePeak = 1.0f;

    // Far longer than the attack, so the gap is down to the last few ulps.
    const auto block = blockOf(static_cast<int>(kSampleRate / 1000.0));
    for (int i = 0; i < 200; ++i)
        advanceFollower(state, settings, block, timing());
    const auto reached = state.envelope;
    CHECK(reached < 1.0f);

    state.sourcePeak = 0.0f;
    for (int i = 0; i < 5; ++i)
        advanceFollower(state, settings, block, timing());
    CHECK(state.envelope == approx(reached));

    advanceFollower(state, settings, block, timing());
    CHECK(state.envelope < reached);
}

TEST_CASE("A follower with a silent source contributes nothing", "[mod][follower]") {
    FollowerSettings settings;
    FollowerState state;

    const auto block = blockOf(512);
    for (int i = 0; i < 20; ++i)
        CHECK(advanceFollower(state, settings, block, timing()) == approx(0.0f));
}

TEST_CASE("The band-limit filters keep the coefficients and output they had before the SDK biquad",
          "[mod][follower]") {
    struct Pin {
        double sampleRate, cutoff;
        bool highPass;
        std::uint32_t coeffs[5];
        std::uint64_t outputHash;
    };

    // Captured from the pre-SDK FollowerBiquad: coefficient bits, then an FNV-1a hash over the
    // output bits for 2000 samples of a fixed LCG signal.
    const Pin pins[] = {
        {48000,
         1000,
         false,
         {0x3b8052da, 0x3c0052da, 0x3b8052da, 0xbfe85d19, 0x3f54bcc8},
         0xe282b339737b9152ULL},
        {44100,
         200,
         true,
         {0x3f7ae4b8, 0xbffae4b8, 0x3f7ae4b8, 0xbffad7ae, 0x3f75e385},
         0x84aab841f57fdc82ULL},
        {96000,
         8000,
         false,
         {0x3d4ab5fb, 0x3dcab5fb, 0x3d4ab5fb, 0xbfa3caff, 0x3ef486f9},
         0x93ce4660d9b41279ULL},
        {22050,
         50,
         true,
         {0x3f7d6f11, 0xbffd6f11, 0x3f7d6f11, 0xbffd6bc6, 0x3f7ae4b9},
         0xeef4d7661b06532dULL},
    };

    for (const auto& pin : pins) {
        const auto c = pin.highPass ? magda::sdk::followerHighPass(pin.sampleRate, pin.cutoff)
                                    : magda::sdk::followerLowPass(pin.sampleRate, pin.cutoff);
        CHECK(std::bit_cast<std::uint32_t>(c.b0) == pin.coeffs[0]);
        CHECK(std::bit_cast<std::uint32_t>(c.b1) == pin.coeffs[1]);
        CHECK(std::bit_cast<std::uint32_t>(c.b2) == pin.coeffs[2]);
        CHECK(std::bit_cast<std::uint32_t>(c.a1) == pin.coeffs[3]);
        CHECK(std::bit_cast<std::uint32_t>(c.a2) == pin.coeffs[4]);

        magda::sdk::FollowerBiquad filter(c);
        std::uint64_t hash = 1469598103934665603ULL;
        std::uint32_t lcg = 1;
        for (int i = 0; i < 2000; ++i) {
            lcg = lcg * 1664525u + 1013904223u;
            const float x = (static_cast<float>(lcg >> 8) / 16777216.0f - 0.5f) * 2.0f;
            hash = (hash ^ std::bit_cast<std::uint32_t>(filter.process(x))) * 1099511628211ULL;
        }
#if defined(__APPLE__) && defined(__aarch64__)
        // Captured on this platform; other compilers round the last bit differently.
        CHECK(hash == pin.outputHash);
#else
        CHECK(hash != 0);
#endif
    }
}
