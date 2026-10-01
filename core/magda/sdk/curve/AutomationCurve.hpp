#pragma once

#include <functional>
#include <optional>
#include <span>

#include "magda/sdk/curve/CurveTypes.hpp"

/**
 * @file AutomationCurve.hpp
 * @brief What a breakpoint list is worth at a beat.
 *
 * The editor draws, the manager plays and the engine bakes (#2118) a curve through
 * these functions, so the three cannot disagree. Pure and allocation-free.
 */

namespace magda::sdk::automation {

/**
 * @brief The value of @p points at @p beat.
 *
 * Points are in beat order. Before the first and after the last the curve holds;
 * an empty list answers the middle of the range. The point that opens a segment shapes it: a
 * linear one through curvemath::evalSegment, a bezier through the parametric cubic solved for
 * the queried beat, a step held, a hard corner as two straight runs meeting at its apex.
 */
double valueAtBeat(std::span<const AutomationPoint> points, double beat);

/** @brief Where a hard corner turns, and what it is worth there. */
struct HardCorner {
    double beat = 0.0;
    double value = 0.0;
};

/**
 * @brief The apex of the hard corner between @p p1 and @p p2, if it is one.
 *
 * The apex is a knot that sampling at the breakpoints alone would miss; the engine's bake
 * needs it (#2118).
 */
std::optional<HardCorner> hardCornerOf(const AutomationPoint& p1, const AutomationPoint& p2);

/**
 * @brief The point that opens the segment @p beat falls in, or null.
 *
 * Null before the first point, at or after the last, and for a curve with fewer than two.
 */
const AutomationPoint* segmentOpening(std::span<const AutomationPoint> points, double beat);

/**
 * @brief The value of a lane at a timeline beat.
 *
 * An absolute lane is @p absolutePoints. A clip-based one is the clip containing the beat, read
 * at its local position so a looping clip repeats; between clips the lane holds the nearest
 * edge. @p getClip resolves an id in @p clipIds; a null return is a clip the lane lists and the
 * project lost, and is skipped.
 */
double laneValueAtBeat(bool isAbsolute, std::span<const AutomationPoint> absolutePoints,
                       std::span<const int> clipIds,
                       const std::function<const AutomationClip*(int)>& getClip, double beat);

}  // namespace magda::sdk::automation
