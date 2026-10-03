#pragma once

#include <algorithm>
#include <cmath>

namespace magda::sdk::sequencer::ramp_curve {

/**
 * @brief Quadratic-bezier step-timing warp, shared by StepClock and the MIDI devices (#2299).
 *
 * Maps linear position t (0..1) through a curve whose control point sits at
 * (skew, skew+depth) in graph space. depth 0 is linear; positive bows the
 * curve above the diagonal (front-loaded), negative below (back-loaded).
 * hardAngle swaps the bezier for two straight segments through the control
 * point.
 */
inline double applyRampCurve(double t, float depth, float skew, bool hardAngle = false) {
    auto d = static_cast<double>(std::clamp(depth, -0.99f, 0.99f));
    auto s = static_cast<double>(std::clamp(0.5 + static_cast<double>(skew) * 0.49, 0.01, 0.99));

    if (std::abs(d) < 0.001)
        return t;

    // Clamp control point ordinate to [0, 1] so the curve stays in bounds
    double cp = std::clamp(s + d, 0.0, 1.0);

    if (hardAngle) {
        // Piecewise linear: two straight segments through control point (s, cp)
        double result = NAN;
        if (t <= s)
            result = t * cp / s;
        else
            result = cp + (t - s) * (1.0 - cp) / (1.0 - s);
        return std::clamp(result, 0.0, 1.0);
    }

    // Quadratic bezier with control point (s, cp)
    double u = NAN;
    double a = 1.0 - 2.0 * s;
    if (std::abs(a) < 1e-10) {
        u = t;
    } else {
        double disc = s * s + a * t;
        u = (-s + std::sqrt(std::max(0.0, disc))) / a;
        u = std::clamp(u, 0.0, 1.0);
    }

    return std::clamp(2.0 * (1.0 - u) * u * cp + u * u, 0.0, 1.0);
}

/** The same curve tiled `cycles` times across [0, 1]. */
inline double applyRampCurveWithCycles(double t, float depth, float skew, int cycles = 1,
                                       bool hardAngle = false) {
    const double clampedT = std::clamp(t, 0.0, 1.0);
    const int cycleCount = std::max(1, cycles);
    if (cycleCount <= 1)
        return applyRampCurve(clampedT, depth, skew, hardAngle);

    const double segLen = 1.0 / static_cast<double>(cycleCount);
    const int seg = std::min(static_cast<int>(clampedT / segLen), cycleCount - 1);
    const double tLocal = (clampedT - seg * segLen) / segLen;
    return (seg + applyRampCurve(tLocal, depth, skew, hardAngle)) * segLen;
}

}  // namespace magda::sdk::sequencer::ramp_curve
