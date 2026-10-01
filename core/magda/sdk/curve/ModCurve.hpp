#pragma once

#include <span>

#include "magda/sdk/curve/CurveTypes.hpp"

/**
 * @file ModCurve.hpp
 * @brief The shape a modulator has at a phase, read in one place.
 *
 * The editor's preview and the engine's LFO (#2119) read a cycle through
 * these functions, so the dot, the sound and the render cannot disagree.
 *
 * Pure: no allocation, no locks, no state; safe on the audio thread. Phase in and level out
 * are both 0 to 1, and the level is the curve as drawn: inverting it is the caller's.
 */

namespace magda::sdk::modcurve {

/** @brief One cycle of a built-in waveform. */
float waveform(LFOWaveform wave, float phase);

/** @brief One cycle of a curve preset, for a custom curve with nothing drawn on it. */
float preset(CurvePreset shape, float phase);

/**
 * @brief One cycle of a drawn curve.
 *
 * The points are in phase order and the curve wraps: a phase past the last
 * point is in the run that closes the cycle at the first. Each point shapes the run leaving it
 * (step, hard corner, or a bend read through curvemath). An empty list answers the middle.
 */
float points(std::span<const CurvePointData> curve, float phase);

/**
 * @brief The level a modulator of this shape has at @p phase.
 *
 * A built-in waveform, a drawn curve, or the preset behind a Custom waveform with no points.
 */
float shapeAt(LFOWaveform wave, CurvePreset shape, std::span<const CurvePointData> curve,
              float phase);

/**
 * @brief Where a cycle ends, which is what a one-shot holds once it is through.
 *
 * The last drawn point's own value: reading just short of 1 interpolates back towards the
 * first point, which a one-shot never arrives at.
 */
float endValue(LFOWaveform wave, CurvePreset shape, std::span<const CurvePointData> curve);

}  // namespace magda::sdk::modcurve
