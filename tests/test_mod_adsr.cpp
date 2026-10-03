#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <magda/sdk/curve/ModCurve.hpp>
#include <support/ModBlocks.hpp>
#include <vector>

/**
 * @file test_mod_adsr.cpp
 * @brief The envelope core (#2120, #2932).
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

/// A block of @p numSamples with the transport rolling, which is what a
/// transport-gated envelope needs and what a free-running one ignores.
Block rollingBlock(int numSamples, bool playing = true) {
    Block block;
    block.numSamples = numSamples;
    block.playing = playing;
    return block;
}

/// A block a millisecond long at the test's sample rate, which makes a stage
/// length in milliseconds a block count.
Block millisecondBlock(bool playing = true) {
    return rollingBlock(static_cast<int>(kSampleRate / 1000.0), playing);
}

/// An envelope with round stage lengths: ten blocks up, ten down to a half, ten
/// back to nothing, when stepped a millisecond at a time.
AdsrSettings tenTenTen(ModSync sync = ModSync::Note) {
    AdsrSettings settings;
    settings.attackMs = 10.0f;
    settings.decayMs = 10.0f;
    settings.sustain = 0.5f;
    settings.releaseMs = 10.0f;
    settings.sync = sync;
    settings.trigger = sync == ModSync::Note ? LFOTriggerMode::MIDI : LFOTriggerMode::Free;
    return settings;
}

float step(AdsrState& state, const AdsrSettings& settings, const Block& block,
           const ModTiming& time = timing()) {
    return advanceAdsr(state, settings, block, time);
}

/// Run @p count blocks and return the last value.
float run(AdsrState& state, const AdsrSettings& settings, int count,
          const Block& block = millisecondBlock()) {
    float value = 0.0f;
    for (int i = 0; i < count; ++i)
        value = step(state, settings, block);
    return value;
}

}  // namespace

TEST_CASE("A segment with no curvature is a straight line", "[mod][adsr]") {
    CHECK(adsrSegmentAt(0.0f, 0.0f, 1.0f, 0.0f) == approx(0.0f));
    CHECK(adsrSegmentAt(0.5f, 0.0f, 1.0f, 0.0f) == approx(0.5f));
    CHECK(adsrSegmentAt(1.0f, 0.0f, 1.0f, 0.0f) == approx(1.0f));

    // Falling as well as rising, and between two values neither of which is an
    // end of the range: a release from a half-open envelope is one of these.
    CHECK(adsrSegmentAt(0.25f, 0.8f, 0.4f, 0.0f) == approx(0.7f));

    // A segment that does not travel has nothing to bend, whatever the
    // curvature says. The bezier would be asked for the y at an x on a line of
    // no height.
    CHECK(adsrSegmentAt(0.5f, 0.3f, 0.3f, 0.4f) == approx(0.3f));
}

TEST_CASE("Curvature bends a segment without moving its ends", "[mod][adsr]") {
    // Whatever the bend, a segment starts where it starts and arrives where it
    // arrives: the curvature is about the route.
    for (const float curve : {-0.5f, -0.25f, 0.25f, 0.5f}) {
        CHECK(adsrSegmentAt(0.0f, 0.0f, 1.0f, curve) == approx(0.0f));
        CHECK(adsrSegmentAt(1.0f, 0.0f, 1.0f, curve) == approx(1.0f));
    }

    // Positive curvature is the exponential side: slow away from the start and
    // fast into the end, so the midpoint sits below the straight line.
    CHECK(adsrSegmentAt(0.5f, 0.0f, 1.0f, 0.5f) < 0.5f);

    // Negative is the logarithmic one, and the two are reflections about the
    // line rather than about each other's shape.
    CHECK(adsrSegmentAt(0.5f, 0.0f, 1.0f, -0.5f) > 0.5f);

    // Monotonic in between: a bent attack still only rises.
    float previous = -1.0f;
    for (int i = 0; i <= 20; ++i) {
        const auto value = adsrSegmentAt(static_cast<float>(i) / 20.0f, 0.0f, 1.0f, 0.35f);
        CHECK(value >= previous);
        previous = value;
    }
}

TEST_CASE("A stage runs in milliseconds unless the envelope is synced", "[mod][adsr]") {
    AdsrSettings settings;
    CHECK(adsrStageSeconds(250.0f, settings, timing()) == Catch::Approx(0.25));

    // Synced, a stage is the division rather than the time, and the division is
    // the one the model carries for all three stages. A quarter at 120 is half
    // a second.
    settings.tempoSync = true;
    settings.rateType = static_cast<int>(ModRateType::Quarter);
    CHECK(adsrStageSeconds(250.0f, settings, timing()) == Catch::Approx(0.5));

    // The signature counts, because a division is a fraction of a bar: a bar of
    // three four is three beats, so a bar-long stage is a second and a half.
    settings.rateType = static_cast<int>(ModRateType::Bar);
    CHECK(adsrStageSeconds(250.0f, settings, timing(120.0, 3, 4)) == Catch::Approx(1.5));

    // Hertz is not a division. The model reaches that by having tempo sync on
    // with nothing musical selected, and the answer is the milliseconds.
    settings.rateType = static_cast<int>(ModRateType::Hertz);
    CHECK(adsrStageSeconds(250.0f, settings, timing()) == Catch::Approx(0.25));
}

TEST_CASE("A gate opening runs the envelope through its stages", "[mod][adsr]") {
    const auto settings = tenTenTen();
    AdsrState state;

    // Shut to begin with, because a MIDI-triggered envelope waits for a note.
    state.gated = true;
    state.started = true;
    state.trigger = settings.trigger;

    CHECK(step(state, settings, millisecondBlock()) == approx(0.0f));
    CHECK(state.stage == AdsrStage::Idle);

    state.gated = false;

    // Advanced first and published after: the
    // value a block renders with is where the envelope ends up. One block into
    // a ten-block attack is a tenth of the way up.
    CHECK(step(state, settings, millisecondBlock()) == approx(0.1f));
    CHECK(state.stage == AdsrStage::Attack);

    // Through the attack and into the decay, which falls from the top to the
    // sustain level over its own ten blocks.
    CHECK(run(state, settings, 9) == approx(1.0f));
    CHECK(state.stage == AdsrStage::Decay);

    CHECK(run(state, settings, 5) == approx(0.75f));
    CHECK(run(state, settings, 5) == approx(0.5f));
    CHECK(state.stage == AdsrStage::Sustain);

    // Held there for as long as the gate stays open, which is what makes it a
    // sustain rather than a stage.
    CHECK(run(state, settings, 50) == approx(0.5f));
    CHECK(state.stage == AdsrStage::Sustain);
}

TEST_CASE("A gate shutting releases from wherever the envelope is", "[mod][adsr]") {
    const auto settings = tenTenTen();

    SECTION("from the sustain") {
        AdsrState state;
        run(state, settings, 25);
        REQUIRE(state.stage == AdsrStage::Sustain);

        state.gated = true;
        CHECK(run(state, settings, 5) == approx(0.25f));
        CHECK(state.stage == AdsrStage::Release);

        CHECK(run(state, settings, 5) == approx(0.0f));
        CHECK(state.stage == AdsrStage::Idle);
    }

    SECTION("from halfway up the attack") {
        AdsrState state;
        const auto reached = run(state, settings, 5);
        REQUIRE(state.stage == AdsrStage::Attack);
        REQUIRE(reached == approx(0.5f));

        // The release starts from where the envelope was, not from the top:
        // that is what makes a gate change click-free from any stage.
        state.gated = true;
        CHECK(run(state, settings, 5) == approx(0.25f));
        CHECK(run(state, settings, 5) == approx(0.0f));
    }
}

TEST_CASE("A free-running envelope cycles without a gate", "[mod][adsr]") {
    const auto settings = tenTenTen(ModSync::Free);
    AdsrState state;

    // No note and no transport, and it runs anyway: the gate is held open, and
    // the sustain is skipped so the cycle is attack, decay, release.
    CHECK(run(state, settings, 10) == approx(1.0f));
    CHECK(run(state, settings, 10) == approx(0.5f));

    // Straight from the sustain into the release rather than resting there.
    CHECK(state.stage == AdsrStage::Release);
    CHECK(run(state, settings, 10) == approx(0.0f));

    // And round again, rather than stopping at idle.
    CHECK(run(state, settings, 5) == approx(0.5f));
    CHECK(state.stage == AdsrStage::Attack);
}

TEST_CASE("A transport-locked envelope is gated by playback", "[mod][adsr]") {
    auto settings = tenTenTen(ModSync::Transport);
    settings.trigger = LFOTriggerMode::Transport;

    AdsrState state;

    // Stopped is shut, and a shut gate that has never opened is silence rather
    // than a release.
    CHECK(run(state, settings, 5, millisecondBlock(false)) == approx(0.0f));
    CHECK(state.stage == AdsrStage::Idle);

    CHECK(run(state, settings, 10, millisecondBlock(true)) == approx(1.0f));

    // Stopping releases, from wherever the envelope had got to.
    CHECK(run(state, settings, 5, millisecondBlock(false)) == approx(0.5f));
    CHECK(state.stage == AdsrStage::Release);
}

TEST_CASE("A stage with no time in it is instant", "[mod][adsr]") {
    auto settings = tenTenTen();
    settings.attackMs = 0.0f;
    AdsrState state;

    // Straight to the top and on into the decay within the same block: an
    // envelope with no attack is a click on purpose, and the rest of the block
    // belongs to the stage after it.
    CHECK(step(state, settings, millisecondBlock()) == approx(0.95f));
    CHECK(state.stage == AdsrStage::Decay);

    SECTION("and an envelope with no time anywhere still terminates") {
        // Every stage instant and free running, so the cycle has no time in it
        // anywhere and the block would walk it for ever. The bound is
        // eight stages, and where in the cycle that leaves the
        // envelope is a consequence of the bound rather than a claim: what
        // matters is that the block ends and the output is a level.
        AdsrSettings instant;
        instant.attackMs = 0.0f;
        instant.decayMs = 0.0f;
        instant.releaseMs = 0.0f;
        instant.sustain = 0.5f;
        instant.sync = ModSync::Free;

        AdsrState spinning;
        const auto value = step(spinning, instant, millisecondBlock());
        CHECK(value >= 0.0f);
        CHECK(value <= 1.0f);
    }
}

TEST_CASE("A tempo-sync toggle mid-stage converts the accumulator instead of reinterpreting it",
          "[mod][adsr][2340]") {
    AdsrSettings settings;
    settings.sync = ModSync::Free;
    settings.attackMs = 2000.0f;

    AdsrState state;

    // A one-second block against a two-second attack is halfway up it.
    CHECK(step(state, settings, rollingBlock(48000)) == approx(0.5f));
    REQUIRE(state.stage == AdsrStage::Attack);
    CHECK(state.stagePhase == approx(0.5f));

    // A bar at 120 in four four is two seconds as well, so the stage length
    // does not move under the flip: only the unit timeInStage is counted in
    // does. Reinterpreting the one second already banked as one bar would
    // overshoot the attack outright; converting it through the phase leaves
    // the envelope where it was and lets it finish across the next second.
    settings.tempoSync = true;
    settings.rateType = static_cast<int>(ModRateType::Bar);

    CHECK(step(state, settings, rollingBlock(48000)) == approx(1.0f).margin(1.0e-3));
}

TEST_CASE("A block longer than a stage lands in the stage after it", "[mod][adsr]") {
    const auto settings = tenTenTen();
    AdsrState state;

    // A block of twelve milliseconds over a ten-millisecond attack: ten of it
    // is the attack and the other two belong to the decay, which is a fifth of
    // the way from the top to the sustain.
    const auto block = rollingBlock(static_cast<int>(kSampleRate * 0.012));
    CHECK(step(state, settings, block) == approx(0.9f));
    CHECK(state.stage == AdsrStage::Decay);
}

TEST_CASE("A retrigger enters the attack from where the envelope was", "[mod][adsr]") {
    const auto settings = tenTenTen();
    AdsrState state;

    run(state, settings, 25);
    REQUIRE(state.stage == AdsrStage::Sustain);

    restartAdsr(state, settings, false);
    CHECK(state.stage == AdsrStage::Attack);
    CHECK(state.value == approx(0.5f));

    // Halfway from the sustain to the top after five of the attack's ten
    // blocks, rather than halfway from silence.
    CHECK(run(state, settings, 5) == approx(0.75f));

    SECTION("or from zero, which is what a cross-track trigger asks for") {
        restartAdsr(state, settings, true);
        CHECK(state.value == approx(0.0f));
        CHECK(run(state, settings, 5) == approx(0.5f));
    }
}

TEST_CASE("A mode change retires the gate the old mode left", "[mod][adsr]") {
    auto settings = tenTenTen();
    settings.trigger = LFOTriggerMode::Audio;
    settings.startGated = true;

    AdsrState state;
    CHECK(run(state, settings, 5) == approx(0.0f));
    REQUIRE(state.gated);

    // Switched to free running. Nothing in the new mode ever opens a gate, so
    // an envelope that kept the old one would be silent for ever.
    settings.sync = ModSync::Free;
    settings.trigger = LFOTriggerMode::Free;
    settings.startGated = false;

    CHECK(run(state, settings, 10) == approx(1.0f));
}
