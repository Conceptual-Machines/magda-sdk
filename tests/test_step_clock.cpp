#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <magda/sdk/sequencer/StepClock.hpp>
#include <vector>

using StepClock = magda::sdk::sequencer::StepClock;
using Approx = Catch::Approx;

// ============================================================================
// Rate conversion
// ============================================================================

TEST_CASE("StepClock rateToBeats returns correct subdivisions", "[stepclock][rate]") {
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::Quarter) == 1.0);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::Eighth) == 0.5);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::Sixteenth) == 0.25);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::ThirtySecond) == 0.125);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::DottedQuarter) == 1.5);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::DottedEighth) == 0.75);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::DottedSixteenth) == 0.375);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::TripletQuarter) == Approx(2.0 / 3.0));
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::TripletEighth) == Approx(1.0 / 3.0));
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::TripletSixteenth) == Approx(0.5 / 3.0));
}

TEST_CASE("StepClock dotted rates are 1.5x their base", "[stepclock][rate]") {
    double quarter = StepClock::rateToBeats(StepClock::Rate::Quarter);
    double dottedQuarter = StepClock::rateToBeats(StepClock::Rate::DottedQuarter);
    REQUIRE(dottedQuarter == Approx(quarter * 1.5));

    double eighth = StepClock::rateToBeats(StepClock::Rate::Eighth);
    double dottedEighth = StepClock::rateToBeats(StepClock::Rate::DottedEighth);
    REQUIRE(dottedEighth == Approx(eighth * 1.5));

    double sixteenth = StepClock::rateToBeats(StepClock::Rate::Sixteenth);
    double dottedSixteenth = StepClock::rateToBeats(StepClock::Rate::DottedSixteenth);
    REQUIRE(dottedSixteenth == Approx(sixteenth * 1.5));
}

TEST_CASE("StepClock triplet rates are 2/3 of their base", "[stepclock][rate]") {
    double quarter = StepClock::rateToBeats(StepClock::Rate::Quarter);
    double tripletQuarter = StepClock::rateToBeats(StepClock::Rate::TripletQuarter);
    REQUIRE(tripletQuarter == Approx(quarter * 2.0 / 3.0));

    double eighth = StepClock::rateToBeats(StepClock::Rate::Eighth);
    double tripletEighth = StepClock::rateToBeats(StepClock::Rate::TripletEighth);
    REQUIRE(tripletEighth == Approx(eighth * 2.0 / 3.0));
}

// ============================================================================
// Ramp curve — bezier mode (smooth)
// ============================================================================

// ============================================================================
// processBlock — reachable directly since the clock took a BlockTiming (#2313)
// ============================================================================

namespace {

/// Drive the clock over a run of equal blocks and collect every step event.
/// 120 bpm and 512-sample blocks at 48 kHz: one block is 512/48000 s, which at
/// 2 beats/sec is 512/24000 beats.
struct ClockRunner {
    static constexpr double kSampleRate = 48000.0;
    static constexpr int kBlockSamples = 512;
    static constexpr double kBeatsPerBlock = kBlockSamples / 24000.0;

    StepClock clock;
    int blockSamples = kBlockSamples;
    double beat = 0.0;
    std::vector<StepClock::StepEvent> events;

    explicit ClockRunner(int samplesPerBlock = kBlockSamples) : blockSamples(samplesPerBlock) {
        clock.setSampleRate(kSampleRate);
    }

    double beatsPerBlock() const {
        return blockSamples / 24000.0;
    }

    /// Blocks needed to cover @p beats of timeline at this block size.
    int blocksFor(double beats) const {
        return static_cast<int>(beats / beatsPerBlock()) + 1;
    }

    void run(int blocks, StepClock::Rate rate, StepClock::Direction dir, int numSteps,
             float swing = 0.0f, bool playing = true) {
        StepClock::StepEvent buffer[16];
        for (int i = 0; i < blocks; ++i) {
            const StepClock::BlockTiming timing{.startBeat = beat,
                                                .endBeat = beat + beatsPerBlock(),
                                                .isPlaying = playing,
                                                .numSamples = blockSamples};
            int n = clock.processBlock(timing, rate, dir, swing, numSteps, buffer, 16);
            for (int e = 0; e < n; ++e)
                events.push_back(buffer[e]);
            beat += beatsPerBlock();
        }
    }

    std::vector<int> stepIndices() const {
        std::vector<int> out;
        out.reserve(events.size());
        for (const auto& e : events)
            out.push_back(e.stepIndex);
        return out;
    }
};

}  // namespace

TEST_CASE("StepClock emits nothing while the transport is stopped", "[stepclock][process]") {
    ClockRunner runner;
    runner.run(64, StepClock::Rate::Sixteenth, StepClock::Direction::Forward, 8, 0.0f,
               /*playing=*/false);
    REQUIRE(runner.events.empty());
    REQUIRE_FALSE(runner.clock.isRunning());
}

TEST_CASE("StepClock steps forward at the requested rate", "[stepclock][process]") {
    ClockRunner runner;
    // One bar of 4/4 at 1/16 = 16 steps = 4 beats.
    runner.run(runner.blocksFor(4.0), StepClock::Rate::Sixteenth, StepClock::Direction::Forward, 8);

    REQUIRE(runner.events.size() >= 16);
    // Consecutive steps sit one sixteenth (0.25 beats) apart.
    for (size_t i = 1; i < runner.events.size(); ++i) {
        REQUIRE(runner.events[i].beatPosition - runner.events[i - 1].beatPosition ==
                Approx(0.25).margin(1e-9));
    }
    // Forward over 8 steps wraps 0..7, 0..7.
    auto indices = runner.stepIndices();
    for (size_t i = 0; i < indices.size(); ++i)
        REQUIRE(indices[i] == static_cast<int>(i % 8));
}

TEST_CASE("StepClock reverse direction walks the pattern backwards", "[stepclock][process]") {
    ClockRunner runner;
    runner.run(runner.blocksFor(2.0), StepClock::Rate::Sixteenth, StepClock::Direction::Reverse, 4);

    auto indices = runner.stepIndices();
    REQUIRE(indices.size() >= 8);
    // First event lands on step 0, then walks down and wraps: 0, 3, 2, 1, 0, ...
    const int expected[] = {0, 3, 2, 1};
    for (size_t i = 0; i < indices.size(); ++i)
        REQUIRE(indices[i] == expected[i % 4]);
}

TEST_CASE("StepClock ping-pong turns around at both ends", "[stepclock][process]") {
    ClockRunner runner;
    runner.run(runner.blocksFor(2.0), StepClock::Rate::Sixteenth, StepClock::Direction::PingPong,
               4);

    auto indices = runner.stepIndices();
    REQUIRE(indices.size() >= 6);
    // 0, 1, 2, 3, 2, 1, 0, 1, ...
    const int expected[] = {0, 1, 2, 3, 2, 1};
    for (size_t i = 0; i < std::min<size_t>(indices.size(), 6); ++i)
        REQUIRE(indices[i] == expected[i]);
}

TEST_CASE("StepClock events carry an in-block time inside the block", "[stepclock][process]") {
    ClockRunner runner;
    runner.run(runner.blocksFor(2.0), StepClock::Rate::Sixteenth, StepClock::Direction::Forward, 8);

    const double blockSecs = ClockRunner::kBlockSamples / ClockRunner::kSampleRate;
    REQUIRE_FALSE(runner.events.empty());
    for (const auto& e : runner.events) {
        REQUIRE(e.timeInBlock >= 0.0);
        REQUIRE(e.timeInBlock < blockSecs);
    }
}

TEST_CASE("StepClock swing pushes a tick later on the grid, never earlier",
          "[stepclock][process]") {
    // Whatever swing survives the emit guard below, it only ever delays: an
    // even tick sits exactly on the sixteenth grid and an odd one after it.
    ClockRunner swung{4096};
    swung.run(swung.blocksFor(4.0), StepClock::Rate::Sixteenth, StepClock::Direction::Forward, 8,
              0.25f);

    REQUIRE(swung.events.size() >= 4);
    bool sawDelayed = false;
    for (const auto& e : swung.events) {
        const double offGrid = e.beatPosition - std::floor(e.beatPosition / 0.25) * 0.25;
        REQUIRE(offGrid >= -1e-9);
        REQUIRE(offGrid < 0.25);
        if (offGrid > 1e-6)
            sawDelayed = true;
    }
    REQUIRE(sawDelayed);
}

TEST_CASE("StepClock plays a swung tick late instead of dropping it", "[stepclock][process]") {
    // Regression for #2313: a tick swung past the block that scheduled it was never played.
    ClockRunner straight;
    straight.run(straight.blocksFor(4.0), StepClock::Rate::Sixteenth, StepClock::Direction::Forward,
                 8, 0.0f);

    ClockRunner swung;
    swung.run(swung.blocksFor(4.0), StepClock::Rate::Sixteenth, StepClock::Direction::Forward, 8,
              0.5f);

    REQUIRE(straight.events.size() >= 16);
    REQUIRE(swung.events.size() == straight.events.size());
    // Same steps, same order - only their timing moved.
    REQUIRE(swung.stepIndices() == straight.stepIndices());
    for (size_t i = 0; i < swung.events.size(); ++i) {
        if (i % 2 == 0)
            REQUIRE(swung.events[i].beatPosition ==
                    Approx(straight.events[i].beatPosition).margin(1e-9));
        else
            REQUIRE(swung.events[i].beatPosition > straight.events[i].beatPosition);
    }
}

TEST_CASE("StepClock keeps a swung tick inside the block that plays it", "[stepclock][process]") {
    ClockRunner swung;
    const double blockSecs = ClockRunner::kBlockSamples / ClockRunner::kSampleRate;
    swung.run(swung.blocksFor(4.0), StepClock::Rate::Sixteenth, StepClock::Direction::Forward, 8,
              0.5f);

    REQUIRE_FALSE(swung.events.empty());
    for (const auto& e : swung.events) {
        REQUIRE(e.timeInBlock >= 0.0);
        REQUIRE(e.timeInBlock < blockSecs);
    }
}

TEST_CASE("StepClock survives an arrangement loop wrapping the beat", "[stepclock][process]") {
    // Beats jump backwards at a loop boundary; the grid plays on from wherever it lands.
    StepClock clock;
    clock.setSampleRate(ClockRunner::kSampleRate);
    StepClock::StepEvent buffer[16];

    int total = 0;
    double beat = 0.0;
    for (int i = 0; i < 400; ++i) {
        // Four bars of 4/4, then wrap to zero.
        if (beat >= 16.0)
            beat = 0.0;
        const StepClock::BlockTiming timing{.startBeat = beat,
                                            .endBeat = beat + ClockRunner::kBeatsPerBlock,
                                            .isPlaying = true,
                                            .numSamples = ClockRunner::kBlockSamples};
        total += clock.processBlock(timing, StepClock::Rate::Sixteenth,
                                    StepClock::Direction::Forward, 0.0f, 8, buffer, 16);
        beat += ClockRunner::kBeatsPerBlock;
    }

    // 400 blocks is 400 * 512/24000 beats = ~8.53 beats of stepping per pass;
    // at a sixteenth per step that is ~34 steps whatever the wrap did.
    REQUIRE(total > 30);
}

TEST_CASE("StepClock stops and resets when the transport stops", "[stepclock][process]") {
    ClockRunner runner;
    const int blocks = runner.blocksFor(1.0);
    runner.run(blocks, StepClock::Rate::Sixteenth, StepClock::Direction::Forward, 8);
    REQUIRE_FALSE(runner.events.empty());

    const size_t whilePlaying = runner.events.size();
    runner.run(blocks, StepClock::Rate::Sixteenth, StepClock::Direction::Forward, 8, 0.0f,
               /*playing=*/false);
    REQUIRE(runner.events.size() == whilePlaying);
    REQUIRE_FALSE(runner.clock.isRunning());
}

// ============================================================================
// Quantize, the event cap and the time bend (#22)
// ============================================================================

namespace {

/// Drive the clock over blocks of an arbitrary musical length.
struct BlockRunner {
    static constexpr double kSampleRate = 48000.0;
    static constexpr int kBlockSamples = 512;

    StepClock clock;
    double beat = 0.0;
    std::vector<StepClock::StepEvent> events;

    BlockRunner() {
        clock.setSampleRate(kSampleRate);
    }

    int block(double beats, int numSteps, int maxEvents,
              const magda::sdk::sequencer::TimeBend& bend, float quantize = 0.0f,
              int quantizeSub = 16, float swing = 0.0f) {
        StepClock::StepEvent buffer[256];
        const StepClock::BlockTiming timing{.startBeat = beat,
                                            .endBeat = beat + beats,
                                            .isPlaying = true,
                                            .numSamples = kBlockSamples};
        const int n =
            clock.processBlock(timing, StepClock::Rate::Sixteenth, StepClock::Direction::Forward,
                               swing, numSteps, buffer, maxEvents, bend, quantize, quantizeSub);
        for (int e = 0; e < n; ++e)
            events.push_back(buffer[e]);
        beat += beats;
        return n;
    }
};

magda::sdk::sequencer::TimeBend bent(magda::sdk::sequencer::TimeBendShape shape, int cycles = 1) {
    auto bend = magda::sdk::sequencer::makeTimeBend(shape);
    bend.on = true;
    bend.cycles = cycles;
    return bend;
}

}  // namespace

TEST_CASE("StepClock plays every step a coarse quantize pulls past the block",
          "[stepclock][process]") {
    // A two-bar cycle with one quantize division snaps each tick to a multiple of 8 beats: steps
    // 16-31 land on the first block's end, which is the next block's start.
    BlockRunner runner;
    const magda::sdk::sequencer::TimeBend twoBars{.bars = 2};
    runner.block(8.0, 32, 256, twoBars, 1.0f, 1);
    for (const auto& event : runner.events)
        REQUIRE(event.beatPosition == 0.0);

    const size_t afterFirst = runner.events.size();
    runner.block(0.5, 32, 256, twoBars, 1.0f, 1);

    std::vector<int> carried;
    for (size_t i = afterFirst; i < runner.events.size(); ++i)
        carried.push_back(runner.events[i].stepIndex);
    for (int step = 16; step < 32; ++step)
        REQUIRE(std::find(carried.begin(), carried.end(), step) != carried.end());
}

TEST_CASE("StepClock keeps a block's earliest steps when the caller's array is full",
          "[stepclock][process]") {
    BlockRunner runner;
    REQUIRE(runner.block(4.0, 16, 4, {}) == 4);
    for (int i = 0; i < 4; ++i)
        REQUIRE(runner.events[static_cast<size_t>(i)].stepIndex == i);
}

TEST_CASE("StepClock brings its position back inside a pattern that shrank",
          "[stepclock][process]") {
    for (auto direction : {StepClock::Direction::Forward, StepClock::Direction::Reverse,
                           StepClock::Direction::PingPong, StepClock::Direction::Random}) {
        ClockRunner runner;
        runner.run(runner.blocksFor(4.0), StepClock::Rate::Sixteenth, direction, 32);
        const size_t beforeShrink = runner.events.size();
        REQUIRE(beforeShrink > 8);

        runner.run(runner.blocksFor(4.0), StepClock::Rate::Sixteenth, direction, 8);
        REQUIRE(runner.events.size() > beforeShrink);
        for (size_t i = beforeShrink; i < runner.events.size(); ++i)
            REQUIRE(runner.events[i].stepIndex < 8);
    }
}

TEST_CASE("StepClock plays the same steps whatever the block size", "[stepclock][process]") {
    const auto bend = bent(magda::sdk::sequencer::TimeBendShape::RushThenDrag, 2);

    BlockRunner whole;
    whole.block(16.0, 16, 256, bend, 0.4f, 32, 0.3f);

    BlockRunner pieces;
    for (int i = 0; pieces.beat < 16.0 - 1e-9; ++i)
        pieces.block(std::min(16.0 - pieces.beat, 0.017 + 0.05 * (i % 5)), 16, 256, bend, 0.4f, 32,
                     0.3f);

    REQUIRE(whole.events.size() == 64);
    REQUIRE(pieces.events.size() == whole.events.size());
    for (size_t i = 0; i < whole.events.size(); ++i) {
        REQUIRE(pieces.events[i].stepIndex == whole.events[i].stepIndex);
        REQUIRE(pieces.events[i].beatPosition == Approx(whole.events[i].beatPosition).margin(1e-9));
    }
}

TEST_CASE("StepClock holds through a flat bend and catches up after it", "[stepclock][bend]") {
    BlockRunner runner;
    for (int i = 0; i < 64; ++i)
        runner.block(0.0625, 16, 256, bent(magda::sdk::sequencer::TimeBendShape::HoldThenCatchUp));

    // The hold reaches back to step 0 too: it plays where the head starts to move.
    REQUIRE(runner.events.size() == 16);
    for (size_t i = 0; i < runner.events.size(); ++i) {
        REQUIRE(runner.events[i].beatPosition >= 2.0);
        REQUIRE(runner.events[i].stepIndex == static_cast<int>(i));
    }
}

TEST_CASE("StepClock reads the pattern backwards through a falling bend", "[stepclock][bend]") {
    BlockRunner runner;
    runner.block(4.0, 16, 256, bent(magda::sdk::sequencer::TimeBendShape::Backwards));

    REQUIRE(runner.events.size() == 16);
    for (size_t i = 1; i < runner.events.size(); ++i) {
        REQUIRE(runner.events[i].beatPosition > runner.events[i - 1].beatPosition);
        REQUIRE(runner.events[i].stepIndex == (runner.events[i - 1].stepIndex + 15) % 16);
    }
}

TEST_CASE("StepClock names the step that plays after the block", "[stepclock][bend]") {
    BlockRunner straight;
    straight.block(1.1, 16, 256, {});
    REQUIRE(straight.clock.getNextStep() == 5);

    // Step 0 opens the catch-up at beat 2, so a block just past it is followed by step 1.
    BlockRunner held;
    held.block(2.05, 16, 256, bent(magda::sdk::sequencer::TimeBendShape::HoldThenCatchUp));
    REQUIRE(held.clock.getNextStep() == 1);
}

TEST_CASE("StepClock's Random direction is fixed by its seed and the tick",
          "[stepclock][process]") {
    StepClock a;
    StepClock b;
    a.setRandomSeed(42);
    b.setRandomSeed(42);
    std::vector<int> seen(8, 0);
    for (long long tick = -50; tick < 200; ++tick) {
        const int step = a.stepForTick(tick, 8, StepClock::Direction::Random);
        REQUIRE(step == b.stepForTick(tick, 8, StepClock::Direction::Random));
        ++seen[static_cast<size_t>(step)];
    }
    for (int count : seen)
        REQUIRE(count > 10);
}
