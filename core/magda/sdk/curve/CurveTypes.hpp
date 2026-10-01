#pragma once

#include <cmath>
#include <vector>

/**
 * @file CurveTypes.hpp
 * @brief The points and enums a curve is made of (docs/curve.md).
 */

namespace magda::sdk {

/** @brief How the run leaving a point is shaped. */
enum class CurveInterpolation {
    /// Straight, or bent by the point's tension or handles.
    Linear,
    /// A parametric cubic through the handles. A phase curve reads it as Linear.
    Bezier,
    /// Holds the point's value until the next point.
    Step,
    /// Two straight runs meeting at an apex. Keep LAST: persisted as its integer value.
    HardCorner
};

/** @brief LFO waveform shapes. Persisted as the integer. */
enum class LFOWaveform { Sine, Triangle, Square, Saw, ReverseSaw, Custom };

/** @brief The cycle behind a Custom waveform that has no points drawn on it. Persisted as the
 * integer. */
enum class CurvePreset {
    Triangle,
    Sine,
    RampUp,
    RampDown,
    SCurve,
    Exponential,
    Logarithmic,
    Custom
};

/** @brief A point on a phase curve: the cycle of an LFO or a sidechain gain shape. */
struct CurvePointData {
    /// Position in the cycle, 0 to 1.
    float phase = 0.0f;

    /// Output level, 0 to 1.
    float value = 0.5f;

    /// Bend of the run leaving this point, nominally -3 to +3.
    float tension = 0.0f;

    /// A CurveInterpolation ordinal. An int because the editor writes a menu index into it.
    int curveType = 0;

    /// Handle offsets from the point, in phase and in level.
    float inHandleX = 0.0f;
    float inHandleY = 0.0f;
    float outHandleX = 0.0f;
    float outHandleY = 0.0f;

    bool operator==(const CurvePointData&) const = default;
};

/// Handles are offsets from their point.
struct BezierHandle {
    double beatOffset = 0.0;

    /// Normalised value offset.
    double value = 0.0;

    /// Mirror the opposite handle when one is moved.
    bool linked = true;

    bool isZero() const {
        return beatOffset == 0.0 && value == 0.0;
    }

    bool operator==(const BezierHandle&) const = default;
};

inline constexpr int kInvalidAutomationPointId = -1;

/** @brief A point on an automation curve, positioned in beats. */
struct AutomationPoint {
    int id = kInvalidAutomationPointId;
    double beatPosition = 0.0;

    /// Normalised, 0 to 1.
    double value = 0.5;

    CurveInterpolation curveType = CurveInterpolation::Linear;
    BezierHandle inHandle;
    BezierHandle outHandle;

    /// Bend of the run leaving this point, nominally -1 (concave) to +1 (convex).
    double tension = 0.0;

    bool operator<(const AutomationPoint& other) const {
        if (beatPosition == other.beatPosition)
            return id < other.id;
        return beatPosition < other.beatPosition;
    }

    /// Identity, not shape: two points are the same point when they share an id.
    bool operator==(const AutomationPoint& other) const {
        return id == other.id;
    }
};

/** @brief Where an automation clip sits on the timeline and the points it holds. */
struct AutomationClip {
    double startBeats = 0.0;
    double lengthBeats = 4.0;
    bool looping = false;
    double loopLengthBeats = 4.0;
    std::vector<AutomationPoint> points;

    double getEndBeats() const {
        return startBeats + lengthBeats;
    }

    bool containsBeat(double beat) const {
        return beat >= startBeats && beat < getEndBeats();
    }

    bool overlapsBeats(double start, double end) const {
        return startBeats < end && getEndBeats() > start;
    }

    /// The clip-local beat of @p globalBeat; a looping clip wraps.
    double getLocalBeat(double globalBeat) const {
        double local = globalBeat - startBeats;
        if (looping && loopLengthBeats > 0.0) {
            local = std::fmod(local, loopLengthBeats);
            if (local < 0.0)
                local += loopLengthBeats;
        }
        return local;
    }

    /// The local beat the clip runs out at, as a limit from the left.
    double getEndLocalBeat() const {
        if (looping && loopLengthBeats > 0.0) {
            // fmod of non-representable length/loop ratios lands ~1e-16 off an exact cycle
            // boundary, so both sides of it count as the boundary.
            constexpr double kBoundaryEpsilon = 0.0001;
            const double local = std::fmod(lengthBeats, loopLengthBeats);
            if (lengthBeats > 0.0 &&
                (local <= kBoundaryEpsilon || loopLengthBeats - local <= kBoundaryEpsilon))
                return loopLengthBeats;
            return local;
        }
        return lengthBeats;
    }
};

}  // namespace magda::sdk
