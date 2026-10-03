#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <magda/sdk/curve/ModCurve.hpp>
#include <set>
#include <support/ModBlocks.hpp>
#include <vector>

/**
 * @file test_mod_random.cpp
 * @brief The random core (#2120, #2932).
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

/// A block a tenth of a step long, so ten of them is one cycle at 1 Hz.
Block tenthBlock() {
    return blockOf(static_cast<int>(kSampleRate / 10.0));
}

RandomSettings stepped(float hz = 1.0f) {
    RandomSettings settings;
    settings.rate.hz = hz;
    return settings;
}

RandomState seeded(std::uint64_t address = 1) {
    RandomState state;
    seedRandom(state, address);
    return state;
}

float step(RandomState& state, const RandomSettings& settings, const Block& block,
           const ModTiming& time = timing()) {
    return advanceRandom(state, settings, block, time);
}

/// The values @p count blocks publish.
std::vector<float> walk(RandomState& state, const RandomSettings& settings, int count,
                        const Block& block = tenthBlock()) {
    std::vector<float> values;
    values.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        values.push_back(step(state, settings, block));
    return values;
}

}  // namespace

TEST_CASE("A stepped walk changes value once per cycle", "[mod][random]") {
    auto settings = stepped();
    auto state = seeded();

    // Held across the step, because a shape of zero is a sample and hold: ten
    // blocks of a tenth each is one cycle, and the value inside it is one value.
    const auto first = walk(state, settings, 10);
    const auto second = walk(state, settings, 10);
    const auto third = walk(state, settings, 10);

    for (const auto& cycle : {first, second, third})
        for (const auto value : cycle)
            CHECK(value == approx(cycle.front()));

    // And a different one two cycles on. Two rather than one, because a held
    // step publishes the value drawn at the wrap before it: the shape control
    // is "how much of the step is spent travelling", and a step
    // that travels for none of itself is a sample and hold one step behind.
    CHECK(third.front() != approx(first.front()));
}

TEST_CASE("Step depth bounds how far one step moves", "[mod][random]") {
    auto settings = stepped();
    settings.stepDepth = 0.2f;

    auto state = seeded(7);
    float previous = walk(state, settings, 10).front();

    // Half the control each way, clipped to the range, which is the
    // arithmetic for a unipolar modulator. A hundred steps is enough for a
    // breach to show if the bound were wrong.
    for (int i = 0; i < 100; ++i) {
        const auto value = walk(state, settings, 10).front();
        CHECK(std::abs(value - previous) <= 0.1f + 1e-4f);
        CHECK(value >= 0.0f);
        CHECK(value <= 1.0f);
        previous = value;
    }
}

TEST_CASE("A full step depth still stays in range", "[mod][random]") {
    auto settings = stepped();
    settings.stepDepth = 1.0f;

    auto state = seeded(11);
    for (int i = 0; i < 200; ++i) {
        const auto value = walk(state, settings, 10).front();
        CHECK(value >= 0.0f);
        CHECK(value <= 1.0f);
    }
}

TEST_CASE("Shape decides how much of a step is spent travelling", "[mod][random]") {
    SECTION("a shape of one ramps the whole way across") {
        auto settings = stepped();
        settings.shape = 1.0f;

        auto state = seeded(3);

        // Past the first wrap, so there is a previous value to travel from.
        walk(state, settings, 10);
        const auto values = walk(state, settings, 10);

        // Monotone across the step, in whichever direction this step went.
        const bool rising = values.back() > values.front();
        for (std::size_t i = 1; i < values.size(); ++i)
            CHECK((rising ? values[i] >= values[i - 1] : values[i] <= values[i - 1]));

        // And it actually moved, rather than being flat in both directions.
        CHECK(values.front() != approx(values.back()));
    }

    SECTION("a shape of zero holds") {
        auto settings = stepped();
        auto state = seeded(3);

        walk(state, settings, 10);
        const auto values = walk(state, settings, 10);
        for (const auto value : values)
            CHECK(value == approx(values.front()));
    }
}

TEST_CASE("Smoothing bends the phase and leaves a held step alone", "[mod][random]") {
    // Smoothing acts on the phase, so it eases the ends of a ramp and does
    // nothing at all to a value that is not travelling.
    auto held = stepped();
    held.smooth = 1.0f;

    auto state = seeded(5);
    walk(state, held, 10);
    const auto values = walk(state, held, 10);
    for (const auto value : values)
        CHECK(value == approx(values.front()));

    SECTION("but changes the route a ramped one takes") {
        auto ramped = stepped();
        ramped.shape = 1.0f;

        auto plain = seeded(5);
        walk(plain, ramped, 10);
        const auto straight = walk(plain, ramped, 10);

        ramped.smooth = 1.0f;
        auto smoothed = seeded(5);
        walk(smoothed, ramped, 10);
        const auto eased = walk(smoothed, ramped, 10);

        // The same two ends, because smoothing is about the phase rather than
        // the values the step travels between.
        REQUIRE(straight.size() == eased.size());
        CHECK(eased.front() == approx(straight.front()));

        // And a different route between them.
        bool differs = false;
        for (std::size_t i = 1; i + 1 < straight.size(); ++i)
            differs = differs || std::abs(eased[i] - straight[i]) > 1e-3f;
        CHECK(differs);
    }
}

TEST_CASE("Noise steps every block and ignores the rate", "[mod][random]") {
    auto settings = stepped();
    settings.type = RandomShape::Noise;

    auto state = seeded(13);

    // Ten blocks inside what would be one step at this rate, and ten different
    // values: the rate stops meaning anything, because there is no longer a
    // step for it to be the length of.
    const auto values = walk(state, settings, 10);

    std::set<float> distinct(values.begin(), values.end());
    CHECK(distinct.size() > 1);

    for (const auto value : values) {
        CHECK(value >= 0.0f);
        CHECK(value <= 1.0f);
    }
}

TEST_CASE("A walk is the same on every run of a project", "[mod][random]") {
    const auto settings = stepped();

    auto first = seeded(42);
    auto second = seeded(42);
    CHECK(walk(first, settings, 60) == walk(second, settings, 60));

    // And two modifiers are independent, which is what seeding from the
    // modifier's own address buys: adjacent addresses must not walk together.
    auto other = seeded(43);
    auto again = seeded(42);
    CHECK(walk(other, settings, 60) != walk(again, settings, 60));
}

TEST_CASE("A synced walk steps on the bar grid", "[mod][random]") {
    auto settings = stepped();
    settings.sync = ModSync::Transport;
    settings.tempoSync = true;
    settings.rate.rateType = static_cast<int>(ModRateType::Bar);

    auto state = seeded();

    // A timeline-locked walk is a function of where the block is, so two of
    // them at one rate step together however playback got there. Block one bar
    // in and the phase is at the top of a step.
    Block block = blockOf(64);
    block.beats.start = 4.0;
    block.beats.end = 4.0 + (64.0 / kSampleRate) * 2.0;

    step(state, settings, block);
    CHECK(state.phase == approx(0.0f));

    // Halfway through the next bar is halfway through the step.
    block.beats.start = 6.0;
    step(state, settings, block);
    CHECK(state.phase == approx(0.5f));
}

TEST_CASE("A free-running walk advances by how long the block was", "[mod][random]") {
    auto settings = stepped(2.0f);
    auto state = seeded();

    // Twentieths of a second at two cycles a second is a tenth of a step each,
    // so the tenth block publishes the last position before the wrap and the
    // eleventh publishes the top of the next step. The value a block renders
    // with is the value at its first sample, which is why the wrap shows up in
    // the block after the one that completed it.
    const auto block = blockOf(static_cast<int>(kSampleRate / 20.0));
    for (int i = 0; i < 10; ++i)
        step(state, settings, block);
    CHECK(state.phase == approx(0.9f));

    step(state, settings, block);
    CHECK(state.phase == approx(0.0f));
}

TEST_CASE("A trigger restarts the walk and takes a step", "[mod][random]") {
    auto settings = stepped();
    settings.sync = ModSync::Note;
    settings.trigger = LFOTriggerMode::MIDI;

    auto state = seeded(17);
    walk(state, settings, 5);
    const auto before = state.current;

    restartRandom(state, settings);
    step(state, settings, tenthBlock());

    // Back to the top of a step, and a new number drawn rather than the ramp
    // being replayed towards the one the walk was already heading for.
    CHECK(state.phase == approx(0.0f));
    CHECK(state.current != approx(before));

    SECTION("and a free-running walk ignores it") {
        auto free = stepped();
        auto ignoring = seeded(17);
        walk(ignoring, free, 5);
        const auto phase = ignoring.phase;

        restartRandom(ignoring, free);
        CHECK(!ignoring.stepPending);
        CHECK(ignoring.phase == approx(phase));
    }
}
