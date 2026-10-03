#pragma once

#include <cstdint>

/**
 * @file ModTypes.hpp
 * @brief The vocabulary the modulator cores share (#2932).
 */

namespace magda::sdk {

/** @brief How a modifier's LFO trigger is gated. Persisted as the integer. */
enum class LFOTriggerMode {
    /// Continuous, never resets.
    Free,
    /// Reset on transport start or loop.
    Transport,
    /// Reset on a MIDI note-on.
    MIDI,
    /// Reset on an audio transient.
    Audio
};

/**
 * @brief Modifier rate type, slow to fast.
 *
 * Pinned: this is the discrete value a tempo-synced modifier's Rate parameter
 * carries, so it lands in project files through automation curves and macro/mod
 * links. MAGDA's Rate control does not expose every value; the unexposed ones
 * exist so a value written by the engine or an imported project round-trips.
 */
enum class ModRateType : int {
    Hertz = 0,
    SixteenBars = 1,
    EightBars = 2,
    FourBars = 3,
    TwoBars = 4,
    Bar = 5,
    DottedHalf = 6,
    Half = 7,
    TripletHalf = 8,
    DottedQuarter = 9,
    Quarter = 10,
    TripletQuarter = 11,
    DottedEighth = 12,
    Eighth = 13,
    TripletEighth = 14,
    DottedSixteenth = 15,
    Sixteenth = 16,
    TripletSixteenth = 17,
    DottedThirtySecond = 18,
    ThirtySecond = 19,
    TripletThirtySecond = 20,
    DottedSixtyFourth = 21,
    SixtyFourth = 22,
    TripletSixtyFourth = 23,
};

/** @brief What drives a modifier's output. */
enum class ModKind : std::uint8_t { Lfo, Adsr, Random, Follower };

/**
 * @brief Where a modifier's phase comes from.
 *
 * Folds the model's separate trigger mode and tempo-sync flags into one choice.
 */
enum class ModSync : std::uint8_t {
    /// A free-running ramp, advanced by block length. Keeps moving while
    /// the transport is stopped.
    Free,

    /// Locked to the timeline, so two LFOs at one rate stay in phase.
    Transport,

    /// A free-running ramp restarted by a trigger (note-on or sidechain).
    Note,
};

/** @brief The rate of one modifier, as the block reads it. */
struct LfoRate {
    /// Cycles per second, when not tempo synced.
    float hz = 1.0f;

    /// A ModRateType ordinal, when tempo synced. Persisted as-is because
    /// it lands in project files via the Rate lane.
    int rateType = static_cast<int>(ModRateType::Hertz);
};

/** @brief Sample rate, tempo and signature a block opens on. */
struct ModTiming {
    double sampleRate = 44100.0;
    double bpm = 120.0;
    int numerator = 4;
    int denominator = 4;
};

/**
 * @brief What a modulator reads of a block, already reduced by the host.
 *
 * The host owns the tempo map, so it resolves the bar position and the bars
 * the block covers; a modulator only advances against them.
 */
struct ModBlock {
    int numSamples = 0;

    /// Whether the transport is rolling.
    bool playing = false;

    /// Where the block opens on the seconds timeline.
    double secondsStart = 0.0;

    /// Where the block opens, in bars on the bar grid it renders in.
    double barPosition = 0.0;

    /// How many bars the block covers, across any tempo or signature change.
    double barsElapsed = 0.0;
};

}  // namespace magda::sdk
