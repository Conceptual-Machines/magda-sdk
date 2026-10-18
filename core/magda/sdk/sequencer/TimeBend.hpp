#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <string_view>

#include "magda/sdk/curve/CurveTypes.hpp"
#include "magda/sdk/state/StateNode.hpp"

/**
 * @file TimeBend.hpp
 * @brief A drawn curve from real time to a read head in the pattern (#22, docs/time-bend.md).
 *
 * Over each cycle the curve maps real time (x) to a position in the pattern (y), both 0 to 1 of
 * the cycle, and an event plays wherever the head crosses it: rising plays as written, steeper
 * rolls, flat holds, falling reads backwards. Pure and allocation-free; safe on the audio thread.
 */

namespace magda::sdk::sequencer {

inline constexpr int kMaxTimeBendPoints = 32;
/// The crossing grid, per cycle. Fixed from beat 0, so a hit never depends on the block size.
inline constexpr int kTimeBendSegments = 512;
inline constexpr int kMaxTimeBendCycles = 8;

/** @brief One breakpoint as a shape is written: @c bend in [-1, 1] bows the segment leaving it. */
struct TimeBendKnot {
    float x = 0.0f;
    float y = 0.0f;
    float bend = 0.0f;
};

/** @brief The shapes a bend editor offers to start from. */
enum class TimeBendShape {
    Straight,
    /// Hits bunch early.
    Rush,
    /// Hits bunch late.
    Drag,
    RushThenDrag,
    HoldThenCatchUp,
    Backwards,
    ThereAndBack
};

/** @brief A time bend: the curve, how much of it is heard, and the span it repeats over. */
struct TimeBend {
    bool on = false;
    /// 0 is the pattern as written, 1 the curve.
    float amount = 1.0f;
    /// The span in bars: 1, 2 or 4.
    int bars = 1;
    /// The curve repeats this many times across the span, 1 to kMaxTimeBendCycles.
    int cycles = 1;
    /// Points in phase order, the first at x = 0 and the last at x = 1. Phase is x, value is y.
    std::array<CurvePointData, kMaxTimeBendPoints> points{
        {{.phase = 0.0f, .value = 0.0f}, {.phase = 1.0f, .value = 1.0f}}};
    int count = 2;

    std::span<const CurvePointData> curve() const {
        return {points.data(), static_cast<std::size_t>(std::clamp(count, 0, kMaxTimeBendPoints))};
    }

    /** @brief Replaces the curve with @p curve, pinned and clamped (see sanitise()). */
    void setCurve(std::span<const CurvePointData> curve);

    /** @brief One pass of the curve in beats, for a meter of @p beatsPerBar. */
    double cycleBeats(double beatsPerBar) const;

    /** @brief The read head at @p phase of the cycle, both 0 to 1, with amount applied. */
    double read(double phase) const;

    /** @brief Whether the bend changes anything: on, with amount, and a curve off the diagonal. */
    bool isActive() const;

    /** @brief Pins the ends to x = 0 and 1, clamps every field, and reads Step runs as Linear. */
    void sanitise();

    bool operator==(const TimeBend&) const = default;
};

/** @brief A curve through @p knots, each bend encoded as the segment's stored shaper. */
TimeBend makeTimeBend(std::span<const TimeBendKnot> knots);

/** @brief The curve of a starting shape, everything else at its default. */
TimeBend makeTimeBend(TimeBendShape shape);

/** @brief The menu label of @p shape. */
std::string_view timeBendShapeName(TimeBendShape shape);

/** @brief @p bend as a @c TimeBend node. */
StateNode writeTimeBend(const TimeBend& bend);

/** @brief The bend a @c TimeBend node holds, sanitised; nullopt for any other node. */
std::optional<TimeBend> readTimeBend(const StateNode& node);

/** @brief Where the read head is at real beat @p beat, in pattern beats. */
inline double readHeadBeat(const TimeBend& bend, double cycleBeats, double beat) {
    if (!bend.isActive() || cycleBeats <= 0.0)
        return beat;
    const double origin = std::floor(beat / cycleBeats) * cycleBeats;
    return origin + bend.read((beat - origin) / cycleBeats) * cycleBeats;
}

/**
 * @brief Calls @p emit for every event the head crosses in real beats [@p t0, @p t1).
 *
 * @p sources is called as `sources(lo, hi, visit)` and calls `visit(position, item...)` for each
 * event at a pattern beat in [lo, hi]; a superset is fine. @p emit gets `(realBeat, item...)`,
 * once per crossing, so a falling curve plays an event again. Pattern beats are absolute: cycle n
 * reads pattern beats [n, n + 1) cycles, bent.
 */
template <typename Sources, typename Emit>
void forEachBentHit(const TimeBend& bend, double cycleBeats, double t0, double t1,
                    Sources&& sources, Emit&& emit) {
    if (!(t1 > t0))
        return;

    if (!bend.isActive() || cycleBeats <= 0.0) {
        sources(t0, t1, [&](double position, const auto&... item) {
            if (position >= t0 && position < t1)
                emit(position, item...);
        });
        return;
    }

    // Per grid cell the head is the line from ra to rb; a crossing plays at its real time on it,
    // half-open in the direction of travel so a boundary event fires once per pass.
    const double dt = cycleBeats / kTimeBendSegments;
    const auto i0 = static_cast<long long>(std::floor(t0 / dt));
    const auto i1 = static_cast<long long>(std::floor(t1 / dt));
    for (long long i = i0; i <= i1; ++i) {
        const auto cycle = static_cast<long long>(
            std::floor(static_cast<double>(i) / static_cast<double>(kTimeBendSegments)));
        const auto segment = static_cast<double>(i - cycle * kTimeBendSegments);
        const double origin = static_cast<double>(cycle) * cycleBeats;
        const double ra = origin + bend.read(segment / kTimeBendSegments) * cycleBeats;
        const double rb = origin + bend.read((segment + 1.0) / kTimeBendSegments) * cycleBeats;
        if (std::abs(rb - ra) < 1.0e-12)
            continue;

        const double ta = static_cast<double>(i) * dt;
        const bool rising = rb > ra;
        sources(std::min(ra, rb), std::max(ra, rb), [&](double position, const auto&... item) {
            if (rising ? !(position >= ra && position < rb) : !(position > rb && position <= ra))
                return;
            const double real = ta + (position - ra) / (rb - ra) * dt;
            if (real >= t0 && real < t1)
                emit(real, item...);
        });
    }
}

}  // namespace magda::sdk::sequencer
