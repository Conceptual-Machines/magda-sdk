#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <magda/sdk/sequencer/GrooveExtraction.hpp>
#include <magda/sdk/sequencer/TimeBend.hpp>
#include <vector>

using magda::sdk::sequencer::extractGroove;
using magda::sdk::sequencer::GrooveExtractionSettings;
using magda::sdk::sequencer::GrooveHit;

namespace {

constexpr double kSampleRate = 44100.0;

Catch::Approx approx(double value, double margin = 1e-3) {
    return Catch::Approx(value).margin(margin);
}

/// Hits at @p bpm sixteenths, each step k shifted by lateness[k % size] / 2 of a step.
std::vector<GrooveHit> swungHits(const std::vector<float>& lateness, int steps, double bpm,
                                 double lead = 0.0) {
    const double step = 60.0 / (bpm * 4.0);
    std::vector<GrooveHit> hits;
    for (int k = 0; k < steps; ++k)
        hits.push_back(
            {(k + 0.5 * lateness[static_cast<std::size_t>(k) % lateness.size()]) * step + lead,
             k % 4 == 0 ? 1.0f : 0.5f});
    return hits;
}

/// Decaying noise bursts at @p starts (seconds) over a quiet floor.
std::vector<float> bursts(const std::vector<GrooveHit>& starts, double seconds) {
    const auto length = static_cast<std::int64_t>(seconds * kSampleRate);
    std::vector<float> audio(static_cast<std::size_t>(length));
    for (std::int64_t at = 0; at < length; ++at) {
        auto hash = static_cast<std::uint32_t>(at) * 2654435761u + 12345u;
        hash ^= hash >> 15;
        hash *= 2246822519u;
        hash ^= hash >> 13;
        const float noise = static_cast<float>(hash >> 8) / 8388608.0f - 1.0f;

        float level = 0.001f;
        for (const auto& hit : starts) {
            const auto age = at - std::llround(hit.seconds * kSampleRate);
            if (age >= 0 && age < 3000)
                level += 0.8f * hit.level * std::exp(-static_cast<float>(age) / 400.0f);
        }
        audio[static_cast<std::size_t>(at)] = noise * level;
    }
    return audio;
}

}  // namespace

TEST_CASE("A straight loop reads as no groove", "[groove-extraction]") {
    const auto hits = swungHits({0.0f}, 32, 120.0);
    GrooveExtractionSettings settings;
    settings.bars = 2;
    const auto groove = extractGroove(hits, 4.0, settings);
    REQUIRE(groove.has_value());
    REQUIRE(groove->lateness.size() == 16);
    for (std::size_t i = 0; i < 16; ++i) {
        CHECK(groove->lateness[i] == approx(0.0));
        CHECK(groove->hits[i] == 2);
    }
    CHECK(groove->bpm == approx(120.0));
}

TEST_CASE("A swing reads back as its lateness, and accents as levels", "[groove-extraction]") {
    const std::vector<float> lateness{0.0f, 0.66f};
    const auto hits = swungHits(lateness, 16, 100.0);
    GrooveExtractionSettings settings;
    settings.bpm = 100.0;
    const auto groove = extractGroove(hits, 2.4, settings);
    REQUIRE(groove.has_value());
    for (std::size_t i = 0; i < 16; ++i) {
        CHECK(groove->lateness[i] == approx(lateness[i % 2]));
        CHECK(groove->accent[i] == approx(i % 4 == 0 ? 1.0 : 0.5));
    }
}

TEST_CASE("Repeats of the figure are averaged", "[groove-extraction]") {
    // bar 1 has step 4 a quarter step late, bar 2 three quarters: the mean is half
    auto hits = swungHits({0.0f}, 32, 120.0);
    const double step = 60.0 / (120.0 * 4.0);
    hits[4].seconds += 0.125 * step;
    hits[20].seconds += 0.375 * step;
    GrooveExtractionSettings settings;
    settings.bars = 2;
    const auto groove = extractGroove(hits, 4.0, settings);
    REQUIRE(groove.has_value());
    CHECK(groove->lateness[4] == approx(0.5));
}

TEST_CASE("The grid starts at the loop's first hit", "[groove-extraction]") {
    // every hit 10 ms in (a detector's lead, a loop cut a little early): the shifts are the
    // groove's, not the offset's
    const std::vector<float> lateness{0.0f, 0.5f};
    auto hits = swungHits(lateness, 16, 120.0, 0.010);
    GrooveExtractionSettings settings;
    settings.bpm = 120.0;
    const auto groove = extractGroove(hits, 2.0, settings);
    REQUIRE(groove.has_value());
    CHECK(groove->originSeconds == approx(0.010));
    CHECK(groove->lateness[0] == approx(0.0));
    CHECK(groove->lateness[1] == approx(0.5));
}

TEST_CASE("A hit past the last step is the next downbeat, early", "[groove-extraction]") {
    auto hits = swungHits({0.0f}, 16, 120.0);
    const double step = 60.0 / (120.0 * 4.0);
    hits.erase(hits.begin() + 1, hits.end());
    hits.push_back({16.0 * step - 0.25 * step, 1.0f});
    GrooveExtractionSettings settings;
    settings.bpm = 120.0;
    const auto groove = extractGroove(hits, 2.0, settings);
    REQUIRE(groove.has_value());
    // the downbeat at 0 is nearer its step than the early one
    CHECK(groove->lateness[0] == approx(0.0));
    CHECK(groove->hits[0] == 1);
}

TEST_CASE("Settings that lay no grid, or no hits, give nothing", "[groove-extraction]") {
    const std::vector<GrooveHit> one{{0.0, 1.0f}};
    GrooveExtractionSettings settings;
    CHECK_FALSE(extractGroove(std::span<const GrooveHit>{}, 2.0, settings).has_value());
    CHECK_FALSE(extractGroove(one, 0.0, settings).has_value());
    settings.bars = 0;
    CHECK_FALSE(extractGroove(one, 2.0, settings).has_value());
    settings.bars = 1;
    settings.notesPerBeat = 0;
    CHECK_FALSE(extractGroove(one, 2.0, settings).has_value());
}

TEST_CASE("A recorded swing comes back from its audio", "[groove-extraction]") {
    const std::vector<float> lateness{0.0f, 0.5f};
    const auto hits = swungHits(lateness, 32, 110.0);
    const double seconds = 2.0 * 4.0 * 60.0 / 110.0;
    const auto audio = bursts(hits, seconds);
    const float* channels[] = {audio.data()};
    const magda::ConstBufferView view(channels, 1, static_cast<int>(audio.size()));

    GrooveExtractionSettings settings;
    settings.bars = 2;
    const auto groove = extractGroove(view, kSampleRate, settings);
    REQUIRE(groove.has_value());
    CHECK(groove->bpm == approx(110.0, 0.01));
    for (std::size_t i = 0; i < 16; ++i) {
        CHECK(groove->hits[i] == 2);
        CHECK(groove->lateness[i] == approx(lateness[i % 2], 0.01));
        CHECK(groove->accent[i] == approx(i % 4 == 0 ? 1.0 : 0.5, 0.1));
    }

    const auto bend = magda::sdk::sequencer::grooveTimeBend(groove->lateness, 4, 4.0);
    CHECK(bend.has_value());
}
