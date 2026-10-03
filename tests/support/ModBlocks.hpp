#pragma once

/**
 * @file ModBlocks.hpp
 * @brief A hand-assembled block for the modulator tests, reduced to a ModBlock the way a host with
 * no tempo map does.
 */

#include <algorithm>
#include <magda/sdk/mod/ModAdsr.hpp>
#include <magda/sdk/mod/ModFollower.hpp>
#include <magda/sdk/mod/ModLfo.hpp>
#include <magda/sdk/mod/ModRandom.hpp>
#include <span>

namespace magda::modtest {

struct Range {
    double start = 0.0;
    double end = 0.0;

    double length() const {
        return end - start;
    }
};

/// The fields of a transport block the modulators read.
struct Block {
    int numSamples = 0;
    bool playing = false;
    Range beats;
    Range seconds;
};

/// The reduction a host with no tempo map performs: one signature, the block's own beat length.
inline sdk::ModBlock toModBlock(const Block& block, const sdk::ModTiming& timing) {
    const double barBeats = sdk::barBeatsOf(timing.numerator, timing.denominator);

    sdk::ModBlock out;
    out.numSamples = block.numSamples;
    out.playing = block.playing;
    out.secondsStart = block.seconds.start;
    out.barPosition =
        block.beats.start / std::max(4.0 * timing.numerator / timing.denominator, 1.0e-6);
    out.barsElapsed = block.playing && block.beats.end > block.beats.start
                          ? block.beats.length() / std::max(barBeats, 1.0e-6)
                          : std::max(block.numSamples, 0) / std::max(timing.sampleRate, 1.0) *
                                timing.bpm / 60.0 / std::max(barBeats, 1.0e-6);
    return out;
}

inline float advanceLfo(sdk::LfoState& state, const sdk::LfoSettings& settings,
                        std::span<const sdk::CurvePointData> curve, const Block& block,
                        const sdk::ModTiming& timing) {
    return sdk::advanceLfo(state, settings, curve, toModBlock(block, timing), timing);
}

inline float advanceAdsr(sdk::AdsrState& state, const sdk::AdsrSettings& settings,
                         const Block& block, const sdk::ModTiming& timing) {
    return sdk::advanceAdsr(state, settings, toModBlock(block, timing), timing);
}

inline float advanceRandom(sdk::RandomState& state, const sdk::RandomSettings& settings,
                           const Block& block, const sdk::ModTiming& timing) {
    return sdk::advanceRandom(state, settings, toModBlock(block, timing), timing);
}

inline float advanceFollower(sdk::FollowerState& state, const sdk::FollowerSettings& settings,
                             const Block& block, const sdk::ModTiming& timing) {
    return sdk::advanceFollower(state, settings, toModBlock(block, timing), timing);
}

}  // namespace magda::modtest
