#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <magda/sdk/sequencer/StepClock.hpp>

namespace magda::sdk::sequencer {

namespace {

/// Ticks one block can hold before ordering; a curve near vertical rolls many into one block.
constexpr int kMaxCandidates = 256;

struct Candidate {
    double beat = 0.0;
    long long tick = 0;
};

std::uint64_t mix(std::uint64_t x) {
    // splitmix64's finaliser.
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

long long positiveMod(long long value, long long divisor) {
    const long long r = value % divisor;
    return r < 0 ? r + divisor : r;
}

}  // namespace

StepClock::StepClock() = default;

void StepClock::reset() {
    running_ = false;
    currentStep_ = 0;
    nextStep_ = 0;
    cycleStep_ = 0;
}

double StepClock::rateToBeats(Rate r) {
    switch (r) {
        case Rate::DottedQuarter:
            return 1.5;
        case Rate::Quarter:
            return 1.0;
        case Rate::TripletQuarter:
            return 2.0 / 3.0;
        case Rate::DottedEighth:
            return 0.75;
        case Rate::Eighth:
            return 0.5;
        case Rate::TripletEighth:
            return 1.0 / 3.0;
        case Rate::DottedSixteenth:
            return 0.375;
        case Rate::Sixteenth:
            return 0.25;
        case Rate::TripletSixteenth:
            return 0.5 / 3.0;
        case Rate::ThirtySecond:
            return 0.125;
        default:
            return 0.5;
    }
}

int StepClock::stepForTick(long long tick, int numSteps, Direction direction) const {
    if (numSteps <= 1)
        return 0;
    const auto n = static_cast<long long>(numSteps);
    switch (direction) {
        case Direction::Forward:
            return static_cast<int>(positiveMod(tick, n));
        case Direction::Reverse:
            return static_cast<int>(positiveMod(-tick, n));
        case Direction::PingPong: {
            const long long period = 2 * n - 2;
            const long long m = positiveMod(tick, period);
            return static_cast<int>(m < n ? m : period - m);
        }
        case Direction::Random:
            return static_cast<int>(mix(randomSeed_ ^ mix(static_cast<std::uint64_t>(tick))) %
                                    static_cast<std::uint64_t>(n));
    }
    return static_cast<int>(positiveMod(tick, n));
}

int StepClock::processBlock(const BlockTiming& timing, Rate rate, Direction direction, float swing,
                            int numSteps, StepEvent* events, int maxEvents, const TimeBend& bend,
                            float quantizeAmount, int quantizeSub) {
    running_ = timing.isPlaying;
    if (!timing.isPlaying || numSteps <= 0 || maxEvents <= 0)
        return 0;

    const double t0 = timing.startBeat;
    const double t1 = timing.endBeat;
    if (!(t1 > t0))
        return 0;

    const double stepBeats = rateToBeats(rate);
    const double swingBeats = static_cast<double>(std::clamp(swing, 0.0f, 1.0f)) * stepBeats * 0.5;
    const double cycleBeats = bend.cycleBeats(timing.beatsPerBar);
    const double quantize = std::clamp(static_cast<double>(quantizeAmount), 0.0, 1.0);
    const double grid =
        quantize > 0.0 && quantizeSub > 0 ? cycleBeats / static_cast<double>(quantizeSub) : 0.0;
    // Quantize moves a hit by up to half its grid, so the hits it can pull in lie that far out.
    const double reach = grid > 0.0 ? 0.5 * grid * quantize : 0.0;

    // Ticks whose swung pattern beat is in [lo, hi].
    const auto ticks = [&](double lo, double hi, const auto& visit) {
        for (auto k = static_cast<long long>(std::floor((lo - swingBeats) / stepBeats)) - 1;
             static_cast<double>(k) * stepBeats <= hi; ++k) {
            const bool odd = k % 2 != 0;
            visit(static_cast<double>(k) * stepBeats + (odd ? swingBeats : 0.0), k);
        }
    };
    const auto quantized = [&](double beat) {
        return grid > 0.0 ? beat + (std::round(beat / grid) * grid - beat) * quantize : beat;
    };

    std::array<Candidate, kMaxCandidates> found{};
    int count = 0;
    forEachBentHit(bend, cycleBeats, t0 - reach, t1 + reach, ticks,
                   [&](double real, long long tick) {
                       const double beat = quantized(real);
                       if (beat >= t0 && beat < t1 && count < kMaxCandidates)
                           found[static_cast<std::size_t>(count++)] = {beat, tick};
                   });

    // A falling run emits later ticks first, and quantize can reorder; insertion sort keeps ties
    // in emission order and does not allocate.
    for (int i = 1; i < count; ++i) {
        const auto held = found[static_cast<std::size_t>(i)];
        int j = i - 1;
        while (j >= 0 && found[static_cast<std::size_t>(j)].beat > held.beat) {
            found[static_cast<std::size_t>(j + 1)] = found[static_cast<std::size_t>(j)];
            --j;
        }
        found[static_cast<std::size_t>(j + 1)] = held;
    }

    const double blockSecs = static_cast<double>(timing.numSamples) / sampleRate_;
    const double lastSampleSecs =
        timing.numSamples > 1 ? static_cast<double>(timing.numSamples - 1) / sampleRate_ : 0.0;
    const int written = std::min(count, maxEvents);
    for (int i = 0; i < written; ++i) {
        const auto& hit = found[static_cast<std::size_t>(i)];
        const double frac = (hit.beat - t0) / (t1 - t0);
        events[i] = {.stepIndex = stepForTick(hit.tick, numSteps, direction),
                     .beatPosition = hit.beat,
                     .timeInBlock = std::min(frac * blockSecs, lastSampleSecs)};
    }
    if (written > 0) {
        const auto& last = found[static_cast<std::size_t>(written - 1)];
        currentStep_ = events[written - 1].stepIndex;
        cycleStep_ = static_cast<int>(positiveMod(last.tick, numSteps));
    }

    // The next hit after the block, which only a block that played needs. The window grows from
    // two steps to a cycle, since a bend can hold for most of one.
    if (written > 0) {
        const double horizon = std::max(2.0 * stepBeats, cycleBeats) + 2.0 * reach;
        double nextBeat = INFINITY;
        long long nextTick = 0;
        for (double ahead = 2.0 * stepBeats; !std::isfinite(nextBeat); ahead *= 2.0) {
            forEachBentHit(bend, cycleBeats, t1 - reach, t1 + std::min(ahead, horizon) + reach,
                           ticks, [&](double real, long long tick) {
                               const double beat = quantized(real);
                               if (beat >= t1 && beat < nextBeat) {
                                   nextBeat = beat;
                                   nextTick = tick;
                               }
                           });
            if (ahead >= horizon)
                break;
        }
        if (std::isfinite(nextBeat))
            nextStep_ = stepForTick(nextTick, numSteps, direction);
    }

    return written;
}

}  // namespace magda::sdk::sequencer
