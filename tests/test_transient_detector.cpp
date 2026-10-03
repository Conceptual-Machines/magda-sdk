#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <magda/sdk/analysis/TransientDetector.hpp>
#include <utility>
#include <vector>

using magda::sdk::detectTransients;
using magda::sdk::TransientDetectionSettings;

namespace {

constexpr double kSampleRate = 44100.0;

/// Half a millisecond, rounded up: the effective rewind at 44100 is 23 samples, not 22.
constexpr int kRewindSamples = 23;

Catch::Approx approx(double value, double margin = 1e-4) {
    return Catch::Approx(value).margin(margin);
}

struct Click {
    std::int64_t at = 0;
    float amplitude = 1.0f;
};

/// Impulses at chosen positions and silence everywhere else.
std::vector<float> clicks(const std::vector<Click>& list, std::int64_t length) {
    std::vector<float> audio(static_cast<std::size_t>(length), 0.0f);
    for (const auto& click : list)
        audio[static_cast<std::size_t>(click.at)] = click.amplitude;
    return audio;
}

/// Decaying noise bursts over a quiet noise floor, so the followers see float arithmetic that a
/// bare impulse never exercises.
std::vector<float> bursts(const std::vector<std::int64_t>& starts, std::int64_t length) {
    std::vector<float> audio(static_cast<std::size_t>(length));
    for (std::int64_t at = 0; at < length; ++at) {
        auto hash = static_cast<std::uint32_t>(at) * 2654435761u + 12345u;
        hash ^= hash >> 15;
        hash *= 2246822519u;
        hash ^= hash >> 13;
        const float noise = static_cast<float>(hash >> 8) / 8388608.0f - 1.0f;

        float level = 0.01f;
        for (const auto start : starts) {
            const auto age = at - start;
            if (age >= 0 && age < 4000)
                level += 0.8f * std::exp(-static_cast<float>(age) / 600.0f);
        }
        audio[static_cast<std::size_t>(at)] = noise * level;
    }
    return audio;
}

std::vector<double> detect(const std::vector<float>& audio, double sampleRate,
                           const TransientDetectionSettings& settings = {}) {
    const float* channel = audio.data();
    return detectTransients(magda::ConstBufferView(&channel, 1, static_cast<int>(audio.size())),
                            sampleRate, settings);
}

/// Reported positions as whole samples, which is exact.
std::vector<std::int64_t> inSamples(const std::vector<double>& seconds, double sampleRate) {
    std::vector<std::int64_t> samples;
    for (const auto time : seconds)
        samples.push_back(std::llround(time * sampleRate));
    return samples;
}

double expected(std::int64_t sample) {
    return static_cast<double>(std::max<std::int64_t>(0, sample - kRewindSamples)) / kSampleRate;
}

}  // namespace

TEST_CASE("A click train is found where its clicks are", "[transients]") {
    const auto transients = detect(clicks({{44100}, {88200}, {132300}}, 200000), kSampleRate);

    REQUIRE(transients.size() == 3);
    REQUIRE(transients[0] == approx(expected(44100)));
    REQUIRE(transients[1] == approx(expected(88200)));
    REQUIRE(transients[2] == approx(expected(132300)));
}

TEST_CASE("A source with nothing in it has no transients", "[transients]") {
    REQUIRE(detect(clicks({}, 200000), kSampleRate).empty());
    REQUIRE(detect({}, kSampleRate).empty());
    REQUIRE(detect(clicks({{44100}}, 200000), 0.0).empty());
}

TEST_CASE("Sensitivity decides how quiet a transient may be", "[transients]") {
    const auto audio = clicks({{44100, 1.0f}, {88200, 0.1f}, {132300, 0.02f}}, 200000);

    TransientDetectionSettings settings;
    settings.sensitivity = 0.0f;
    REQUIRE(detect(audio, kSampleRate, settings).size() == 1);
    settings.sensitivity = 0.5f;
    REQUIRE(detect(audio, kSampleRate, settings).size() == 2);
    settings.sensitivity = 1.0f;
    REQUIRE(detect(audio, kSampleRate, settings).size() == 3);
}

TEST_CASE("A quiet recording has the same transients as a loud one", "[transients]") {
    const auto loud = clicks({{44100, 1.0f}, {88200, 1.0f}}, 200000);
    const auto quiet = clicks({{44100, 0.02f}, {88200, 0.02f}}, 200000);

    REQUIRE(detect(loud, kSampleRate).size() == detect(quiet, kSampleRate).size());
}

TEST_CASE("Transients closer together than the spacing rule are thinned", "[transients]") {
    SECTION("two inside the retrigger lockout only fire once") {
        const auto transients = detect(clicks({{44100}, {44100 + 1323}}, 200000), kSampleRate);

        REQUIRE(transients.size() == 1);
        REQUIRE(transients[0] == approx(expected(44100)));
    }

    SECTION("two past the lockout but inside the spacing keep the later") {
        TransientDetectionSettings settings;
        settings.minimumSpacingSeconds = 0.15;

        const auto transients =
            detect(clicks({{44100}, {44100 + 5000}}, 200000), kSampleRate, settings);

        REQUIRE(transients.size() == 1);
        REQUIRE(transients[0] == approx(expected(44100 + 5000)));
    }

    SECTION("two beyond the spacing both survive") {
        REQUIRE(detect(clicks({{44100}, {44100 + 8820}}, 200000), kSampleRate).size() == 2);
    }
}

TEST_CASE("Detection is deterministic", "[transients]") {
    const auto audio = clicks({{44100}, {88200}, {132300}}, 200000);

    REQUIRE(detect(audio, kSampleRate) == detect(audio, kSampleRate));
}

TEST_CASE("A block reader and a buffer give the same positions", "[transients]") {
    const auto audio = bursts({20000, 32768 + 2, 70000, 99000}, 150000);
    const auto read = [&audio](float* destination, std::int64_t start, int count) {
        const auto available = static_cast<int>(
            std::clamp<std::int64_t>(static_cast<std::int64_t>(audio.size()) - start, 0, count));
        std::copy_n(audio.begin() + start, available, destination);
        std::fill(destination + available, destination + count, 0.0f);
        return available;
    };

    REQUIRE(detectTransients(read, static_cast<std::int64_t>(audio.size()), kSampleRate, {}) ==
            detect(audio, kSampleRate));
}

TEST_CASE("Detected positions are pinned to what magda-core produced", "[transients]") {
    using Samples = std::vector<std::int64_t>;

    // Captured from magda-core's detector before it moved here. The bursts and the clicks
    // near a 32768-sample block edge pin the block-relative rewind.
    struct Golden {
        float sensitivity;
        Samples levels;
        Samples boundary;
        Samples bursts44100;
        Samples bursts48000;
        Samples bursts96000;
    };
    const Golden goldens[] = {
        {0.0f,
         {44077},
         {32768, 65542},
         {19978, 32768, 69977, 131072, 179977},
         {19977, 32768, 69976, 131072, 179976},
         {19953, 32768, 69952, 131072, 179952}},
        {0.5f,
         {44077, 88177},
         {32768, 65542, 99976},
         {19978, 32768, 69977, 98977, 131072, 179977},
         {19977, 32768, 69976, 98976, 131072, 179976},
         {19953, 32768, 69952, 98952, 131072, 179952}},
        {1.0f,
         {44077, 88177, 132277},
         {32768, 65542, 99976},
         {0, 19977, 32768, 69977, 98977, 131072, 179977},
         {0, 19976, 32768, 69976, 98976, 131072, 179976},
         {0, 19952, 32768, 69952, 98952, 131072, 179952}},
    };

    const std::vector<std::int64_t> burstStarts{20000,  32768 + 2, 70000,
                                                99000,  131072 + 10, 180000};
    const auto levels = clicks({{44100, 1.0f}, {88200, 0.1f}, {132300, 0.02f}}, 200000);
    const auto boundary =
        clicks({{32768 + 5, 1.0f}, {65536 + 30, 0.5f}, {100000, 0.3f}}, 150000);
    const auto burstAudio = bursts(burstStarts, 260000);

    for (const auto& golden : goldens) {
        TransientDetectionSettings settings;
        settings.sensitivity = golden.sensitivity;
        INFO("sensitivity " << golden.sensitivity);

        CHECK(inSamples(detect(levels, kSampleRate, settings), kSampleRate) == golden.levels);
        CHECK(inSamples(detect(boundary, 48000.0, settings), 48000.0) == golden.boundary);

        const std::pair<double, const Samples*> rates[] = {{44100.0, &golden.bursts44100},
                                                           {48000.0, &golden.bursts48000},
                                                           {96000.0, &golden.bursts96000}};
        for (const auto& [rate, expectedSamples] : rates) {
            INFO("rate " << rate);
            CHECK(inSamples(detect(burstAudio, rate, settings), rate) == *expectedSamples);
        }
    }
}
