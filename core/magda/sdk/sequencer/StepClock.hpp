#pragma once

#include <cstdint>
#include <magda/sdk/sequencer/StepPattern.hpp>
#include <magda/sdk/sequencer/TimeBend.hpp>

namespace magda::sdk::sequencer {

/**
 * @brief Tempo-synced step clock used by MIDI sequencer devices.
 *
 * Stateless (#22): tick k sits at pattern beat k * rate from beat 0, swung in pattern time, and
 * plays wherever the time bend's read head crosses it. A tick's step comes from k and the
 * direction alone, so where a block starts, a loop wrap or an offline render never changes what
 * plays.
 *
 * Engine-neutral since #2313: the block's musical extent arrives as a BlockTiming, so the clock
 * never asks a host for a tempo sequence and both engines' devices drive the same code.
 */
class StepClock {
  public:
    // --- Rate divisions ---
    enum class Rate {
        DottedQuarter = 0,
        Quarter,
        TripletQuarter,
        DottedEighth,
        Eighth,
        TripletEighth,
        DottedSixteenth,
        Sixteenth,
        TripletSixteenth,
        ThirtySecond
    };

    // --- Direction modes ---
    enum class Direction { Forward = 0, Reverse, PingPong, Random };

    /**
     * @brief One block's musical extent, as the caller's engine resolved it.
     *
     * Beats at the block's start and end, whether the transport is running, the block length in
     * samples (which, with the clock's sample rate, gives an event's offset in seconds), and the
     * meter the time bend's span is counted in.
     */
    struct BlockTiming {
        double startBeat = 0.0;
        double endBeat = 0.0;
        bool isPlaying = false;
        int numSamples = 0;
        double beatsPerBar = 4.0;
    };

    // --- Step event emitted by processBlock ---
    struct StepEvent {
        int stepIndex;        // Which step fired (0-based, within sequence length)
        double beatPosition;  // Absolute beat position of this step
        double timeInBlock;   // Time offset in seconds from block start
    };

    StepClock();

    /** @brief Forgets what the UI was shown; the clock itself keeps nothing. */
    void reset();

    /** @brief Seeds the Random direction, so a test can pin what a pattern plays. */
    void setRandomSeed(std::int64_t seed) {
        randomSeed_ = static_cast<std::uint64_t>(seed);
    }

    /** @brief Sets the sample rate (call from plugin::initialise). */
    void setSampleRate(double sr) {
        sampleRate_ = sr;
    }

    /**
     * @brief The steps that fire in one block, in time order.
     *
     * @param swing      0-1, delays every odd tick by up to half a step, in pattern time
     * @param numSteps   active steps in the sequence
     * @param bend       the read-head curve the ticks play through
     * @param quantizeAmount 0-1 pull of the played ticks toward @p quantizeSub divisions of the
     *                   bend's cycle
     * @return           events written; past @p maxEvents the latest of a block are dropped
     */
    int processBlock(const BlockTiming& timing, Rate rate, Direction direction, float swing,
                     int numSteps, StepEvent* events, int maxEvents, const TimeBend& bend = {},
                     float quantizeAmount = 0.0f, int quantizeSub = 16);

    /** @brief The last step played (for UI display). */
    int getCurrentStep() const {
        return currentStep_;
    }

    /** @brief The step that plays after the last block, for a gate that has to reach it. */
    int getNextStep() const {
        return nextStep_;
    }

    /** @brief Whether the last block had the transport playing. */
    bool isRunning() const {
        return running_;
    }

    /** @brief The last step's tick within the pattern, 0 to numSteps - 1, for the UI sweep. */
    int getCycleStep() const {
        return cycleStep_;
    }

    /** @brief The pattern step tick @p tick plays. */
    int stepForTick(long long tick, int numSteps, Direction direction) const;

    /** @brief Convert rate enum to beats per step. */
    static double rateToBeats(Rate r);

  private:
    double sampleRate_ = 44100.0;
    std::uint64_t randomSeed_ = 0x853C49E6748FEA9BULL;

    bool running_ = false;
    int currentStep_ = 0;
    int nextStep_ = 0;
    int cycleStep_ = 0;
};

}  // namespace magda::sdk::sequencer
