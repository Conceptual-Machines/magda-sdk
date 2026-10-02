#pragma once

#include <algorithm>
#include <cmath>

/**
 * @file CurveMath.hpp
 * @brief How a bent curve segment is shaped, in one place (docs/curve.md).
 *
 * The editor, the modulator engine and the audio snapshot all call this, so they draw and play
 * the same curve. Pure: no allocation, no locks; safe on the audio thread.
 */

namespace magda::sdk::curvemath {

/**
 * @brief The value of a Linear-type segment at @p t in [0, 1] between endpoint values @p y1 and @p
 * y2.
 *
 * With @p hasStoredShaper the bend is a quadratic control point at @p controlY, rendered as a
 * bounded monotonic power warp through the on-curve midpoint, so it flattens against the [0, 1]
 * boundary instead of overshooting. Without it the bend is the @p tension scalar.
 */
inline float evalSegment(float y1, float y2, float controlY, float tension, bool hasStoredShaper,
                         float t) {
    const float dy = y2 - y1;
    if (std::abs(dy) < 1.0e-6f)
        return y1;

    float r = NAN;  // the curve at t = 0.5, normalised within [y1, y2]
    if (hasStoredShaper) {
        // B(0.5) of the encoding quadratic.
        const float midY = 0.25f * y1 + 0.5f * controlY + 0.25f * y2;
        const float rRaw = (midY - y1) / dy;

        // Fold values outside (0, 1) so dragging past an endpoint keeps bending.
        constexpr float kEps = 1.0e-4f;
        if (rRaw <= 0.0f)
            r = kEps * std::exp(rRaw * 5.0f);
        else if (rRaw >= 1.0f)
            r = 1.0f - kEps * std::exp((1.0f - rRaw) * 5.0f);
        else
            r = std::clamp(rRaw, kEps, 1.0f - kEps);
    } else {
        if (std::abs(tension) < 0.001f)
            return y1 + t * dy;
        r = (tension > 0.0f) ? std::pow(0.5f, 1.0f + tension * 2.0f)
                             : 1.0f - std::pow(0.5f, 1.0f - tension * 2.0f);
        r = std::clamp(r, 1.0e-4f, 1.0f - 1.0e-4f);
    }

    const float inv = 1.0f / std::log(0.5f);
    const float warp = (r <= 0.5f) ? std::pow(t, std::log(r) * inv)
                                   : 1.0f - std::pow(1.0f - t, std::log(1.0f - r) * inv);
    return y1 + dy * warp;
}

}  // namespace magda::sdk::curvemath
