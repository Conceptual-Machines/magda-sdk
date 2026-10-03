#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <magda/sdk/curve/ModCurve.hpp>
#include <support/ModBlocks.hpp>
#include <vector>

/**
 * @file test_mod_lfo.cpp
 * @brief The LFO core: shapes, rates, triggers and gates (#2119, #2932).
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

/// A block of @p numSamples that does not move the timeline, which is what a
/// free-running LFO is measured against: its ramp is real time rather than
/// musical time, and a stopped transport still turns it.
Block stoppedBlock(int numSamples) {
    Block block;
    block.numSamples = numSamples;
    return block;
}

/// A block sitting at @p startBeat, with the seconds face of the same instant
/// worked out at @p bpm, the way the transport's clock would.
Block blockAt(double startBeat, double beats, int numSamples, double bpm = 120.0) {
    Block block;
    block.numSamples = numSamples;
    block.playing = true;
    block.beats.start = startBeat;
    block.beats.end = startBeat + beats;
    block.seconds.start = startBeat * 60.0 / bpm;
    block.seconds.end = block.beats.end * 60.0 / bpm;
    return block;
}

LfoSettings freeRunning(float hz, LFOWaveform wave = LFOWaveform::Saw) {
    LfoSettings settings;
    settings.wave = wave;
    settings.sync = ModSync::Free;
    settings.rate.hz = hz;
    return settings;
}

/// One block of @p settings, and the output it published.
float step(LfoState& state, const LfoSettings& settings, const Block& block,
           std::span<const CurvePointData> curve = {}, const ModTiming& time = timing()) {
    return advanceLfo(state, settings, curve, block, time);
}

}  // namespace

TEST_CASE("Every waveform is read at the same points in the cycle", "[mod][lfo]") {
    const auto at = [](LFOWaveform wave, float phase) {
        LfoState state;
        auto settings = freeRunning(1.0f, wave);
        settings.phaseOffset = phase;
        // A block with no samples in it cannot advance anything, so what comes
        // out is the shape at the phase and nothing else.
        return step(state, settings, stoppedBlock(0));
    };

    SECTION("sine rises from the middle") {
        CHECK(at(LFOWaveform::Sine, 0.0f) == approx(0.5f));
        CHECK(at(LFOWaveform::Sine, 0.25f) == approx(1.0f));
        CHECK(at(LFOWaveform::Sine, 0.5f) == approx(0.5f));
        CHECK(at(LFOWaveform::Sine, 0.75f) == approx(0.0f));
    }

    SECTION("triangle peaks halfway") {
        CHECK(at(LFOWaveform::Triangle, 0.0f) == approx(0.0f));
        CHECK(at(LFOWaveform::Triangle, 0.5f) == approx(1.0f));
        CHECK(at(LFOWaveform::Triangle, 0.75f) == approx(0.5f));
    }

    SECTION("square is high for the first half") {
        CHECK(at(LFOWaveform::Square, 0.0f) == approx(1.0f));
        CHECK(at(LFOWaveform::Square, 0.49f) == approx(1.0f));
        CHECK(at(LFOWaveform::Square, 0.5f) == approx(0.0f));
    }

    SECTION("saw climbs and reverse saw falls") {
        CHECK(at(LFOWaveform::Saw, 0.25f) == approx(0.25f));
        CHECK(at(LFOWaveform::ReverseSaw, 0.25f) == approx(0.75f));
    }
}

TEST_CASE("A drawn cycle is read through the model's own curve", "[mod][lfo]") {
    LfoSettings settings = freeRunning(1.0f, LFOWaveform::Custom);

    // A step and a ramp, which is a shape the built-in waveforms cannot make
    // and which the custom waveform reads.
    std::vector<CurvePointData> curve(3);
    curve[0] = CurvePointData{0.0f, 0.0f};
    curve[0].curveType = 2;  // Step: holds until the next point
    curve[1] = CurvePointData{0.5f, 1.0f};
    curve[2] = CurvePointData{0.75f, 0.0f};

    const auto at = [&](float phase) {
        LfoState state;
        auto shaped = settings;
        shaped.phaseOffset = phase;
        return step(state, shaped, stoppedBlock(0), curve);
    };

    CHECK(at(0.25f) == approx(0.0f));
    CHECK(at(0.5f) == approx(1.0f));
    CHECK(at(0.625f) == approx(magda::sdk::modcurve::points(curve, 0.625f)));
}

TEST_CASE("A custom waveform with nothing drawn on it falls back to its preset", "[mod][lfo]") {
    LfoState state;
    auto settings = freeRunning(1.0f, LFOWaveform::Custom);
    settings.preset = CurvePreset::RampDown;
    settings.phaseOffset = 0.25f;

    CHECK(step(state, settings, stoppedBlock(0)) == approx(0.75f));
}

TEST_CASE("A free-running LFO advances by how long the block lasted", "[mod][lfo]") {
    LfoState state;
    const auto settings = freeRunning(2.0f);  // two cycles a second

    // A quarter of a second at 48k, which is half a cycle at 2 Hz.
    const auto block = stoppedBlock(12000);

    CHECK(step(state, settings, block) == approx(0.0f));
    CHECK(step(state, settings, block) == approx(0.5f));
    CHECK(step(state, settings, block) == approx(0.0f));
}

TEST_CASE("A free-running LFO keeps turning while the transport is stopped", "[mod][lfo]") {
    LfoState state;
    const auto settings = freeRunning(1.0f);

    // The block does not move the timeline at all, which is what a stopped
    // transport renders. The graph is still processing, so the LFO still moves.
    const auto block = stoppedBlock(12000);
    step(state, settings, block);

    CHECK(step(state, settings, block) == approx(0.25f));

    SECTION("and so does a tempo-synced one, at the tempo the cursor sits on") {
        LfoSettings synced;
        synced.wave = LFOWaveform::Saw;
        synced.sync = ModSync::Free;
        synced.tempoSync = true;
        synced.rate.rateType = static_cast<int>(ModRateType::Bar);

        // A stopped block covers no beats, so what it is worth is how long it
        // lasted at the tempo the cursor is on: a quarter of a second at 120 is
        // half a beat, which is an eighth of a four four bar.
        LfoState turning;
        step(turning, synced, block);

        CHECK(step(turning, synced, block) == approx(0.125f));
    }
}

TEST_CASE("A transport-locked LFO is a function of where the block is", "[mod][lfo]") {
    auto settings = freeRunning(1.0f);
    settings.sync = ModSync::Transport;

    // Half a second in at 1 Hz is halfway through the cycle, whatever was
    // rendered before it: two LFOs at one rate agree however playback got here.
    LfoState fresh;
    CHECK(step(fresh, settings, blockAt(1.0, 0.5, 512)) == approx(0.5f));

    LfoState played;
    step(played, settings, blockAt(0.0, 0.5, 512));
    CHECK(step(played, settings, blockAt(1.0, 0.5, 512)) == approx(0.5f));
}

TEST_CASE("The phase offset moves where the cycle is read", "[mod][lfo]") {
    LfoState state;
    auto settings = freeRunning(1.0f);
    settings.phaseOffset = 0.25f;

    CHECK(step(state, settings, stoppedBlock(12000)) == approx(0.25f));
    CHECK(step(state, settings, stoppedBlock(12000)) == approx(0.5f));
}

TEST_CASE("A one-shot plays through and holds where it ended", "[mod][lfo]") {
    LfoState state;
    auto settings = freeRunning(1.0f, LFOWaveform::Triangle);
    settings.sync = ModSync::Note;
    settings.oneShot = true;

    const auto quarter = stoppedBlock(12000);

    CHECK(step(state, settings, quarter) == approx(0.0f));
    CHECK(step(state, settings, quarter) == approx(0.5f));
    CHECK(step(state, settings, quarter) == approx(1.0f));
    CHECK(step(state, settings, quarter) == approx(0.5f));

    // Through. A triangle ends where it started, and that is what is held
    // rather than the wrap-around value the cycle would carry on into.
    CHECK(step(state, settings, quarter) == approx(0.0f));
    CHECK(state.completed);
    CHECK(state.phase == approx(1.0f));
    CHECK(step(state, settings, quarter) == approx(0.0f));

    SECTION("and a trigger plays it again") {
        restartLfo(state, settings);
        CHECK_FALSE(state.completed);
        CHECK(step(state, settings, quarter) == approx(0.0f));
        CHECK(step(state, settings, quarter) == approx(0.5f));
    }
}

TEST_CASE("A sustain loop plays the intro once and then repeats the region", "[mod][lfo]") {
    LfoState state;
    auto settings = freeRunning(1.0f, LFOWaveform::Custom);
    settings.oneShot = true;
    settings.useLoopRegion = true;
    settings.loopStart = 0.5f;
    settings.loopEnd = 0.75f;

    // A straight ramp, so the value reads back as the position in the cycle.
    std::vector<CurvePointData> curve(2);
    curve[0] = CurvePointData{0.0f, 0.0f};
    curve[1] = CurvePointData{1.0f, 1.0f};

    const auto eighth = stoppedBlock(6000);

    CHECK(step(state, settings, eighth, curve) == approx(0.0f));
    CHECK(step(state, settings, eighth, curve) == approx(0.125f));
    CHECK(step(state, settings, eighth, curve) == approx(0.25f));
    CHECK(step(state, settings, eighth, curve) == approx(0.375f));

    // Into the region, and back to its start rather than on past its end.
    CHECK(step(state, settings, eighth, curve) == approx(0.5f));
    CHECK(step(state, settings, eighth, curve) == approx(0.625f));
    CHECK(step(state, settings, eighth, curve) == approx(0.5f));
    CHECK(step(state, settings, eighth, curve) == approx(0.625f));

    // A loop sustains rather than finishing, so the one-shot never latches.
    CHECK_FALSE(state.completed);
}

TEST_CASE("An LFO that is not listening for a trigger ignores one", "[mod][lfo][trigger]") {
    for (const auto sync : {ModSync::Free, ModSync::Transport}) {
        LfoState state;
        auto settings = freeRunning(1.0f);
        settings.sync = sync;

        state.cycles = 0.5;
        restartLfo(state, settings);
        CHECK(state.cycles == Catch::Approx(0.5));
    }
}
