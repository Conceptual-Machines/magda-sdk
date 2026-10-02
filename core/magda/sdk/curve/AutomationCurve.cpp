#include "magda/sdk/curve/AutomationCurve.hpp"

#include <algorithm>
#include <cmath>

#include "magda/sdk/curve/CurveMath.hpp"

namespace magda::sdk::automation {

namespace {

/// Binary search: called per block per automated parameter on a lane as long as the arrangement.
const AutomationPoint* openingBefore(std::span<const AutomationPoint> points, double beat) {
    const auto after = std::upper_bound(
        points.begin(), points.end(), beat,
        [](double value, const AutomationPoint& point) { return value < point.beatPosition; });

    if (after == points.begin() || after == points.end())
        return nullptr;

    return &*(after - 1);
}

double interpolateBezier(double t, const AutomationPoint& p1, const AutomationPoint& p2) {
    // The editor draws the parametric cubic (handles offset x too): solve the x cubic for the
    // parameter at the queried beat, then evaluate the value cubic there.
    const double x0 = p1.beatPosition;
    const double x3 = p2.beatPosition;
    const double x1 = x0 + p1.outHandle.beatOffset;
    const double x2 = x3 + p2.inHandle.beatOffset;
    const double target = x0 + t * (x3 - x0);

    const auto xAt = [&](double s) {
        const double ms = 1.0 - s;
        return ms * ms * ms * x0 + 3.0 * ms * ms * s * x1 + 3.0 * ms * s * s * x2 + s * s * s * x3;
    };
    const auto dxAt = [&](double s) {
        const double ms = 1.0 - s;
        return 3.0 * ms * ms * (x1 - x0) + 6.0 * ms * s * (x2 - x1) + 3.0 * s * s * (x3 - x2);
    };

    // Newton seeded with t, clamped so overshooting handles stay in the segment.
    double s = t;
    for (int i = 0; i < 12; ++i) {
        const double err = xAt(s) - target;
        if (std::abs(err) < 1.0e-6 * (x3 - x0 + 1.0))
            break;
        const double slope = dxAt(s);
        if (std::abs(slope) < 1.0e-9)
            break;
        s = std::clamp(s - err / slope, 0.0, 1.0);
    }

    const double s2 = s * s;
    const double s3 = s2 * s;
    const double ms = 1.0 - s;
    const double ms2 = ms * ms;
    const double ms3 = ms2 * ms;
    const double cp1Value = p1.value + p1.outHandle.value;
    const double cp2Value = p2.value + p2.inHandle.value;
    return ms3 * p1.value + 3.0 * ms2 * s * cp1Value + 3.0 * ms * s2 * cp2Value + s3 * p2.value;
}

/// Linear-type segments go through curvemath::evalSegment, as the editor draws them; @p t is the
/// x-fraction along the segment.
double evalLinearSegment(double t, const AutomationPoint& p1, const AutomationPoint& p2) {
    const bool hasShaper = !p1.outHandle.isZero() || !p2.inHandle.isZero();
    const double controlY = p1.value + p1.outHandle.value;
    return static_cast<double>(curvemath::evalSegment(
        static_cast<float>(p1.value), static_cast<float>(p2.value), static_cast<float>(controlY),
        static_cast<float>(p1.tension), hasShaper, static_cast<float>(t)));
}

}  // namespace

std::optional<HardCorner> hardCornerOf(const AutomationPoint& p1, const AutomationPoint& p2) {
    if (p1.curveType != CurveInterpolation::HardCorner)
        return std::nullopt;

    const double duration = p2.beatPosition - p1.beatPosition;
    if (duration <= 0.0)
        return std::nullopt;

    // The dragged apex, else the midpoint.
    if (p1.outHandle.isZero())
        return HardCorner{p1.beatPosition + 0.5 * duration, (p1.value + p2.value) * 0.5};

    const double apexT = std::clamp(p1.outHandle.beatOffset / duration, 1.0e-4, 1.0 - 1.0e-4);
    return HardCorner{p1.beatPosition + apexT * duration, p1.value + p1.outHandle.value};
}

const AutomationPoint* segmentOpening(std::span<const AutomationPoint> points, double beat) {
    if (points.size() < 2 || beat < points.front().beatPosition ||
        beat >= points.back().beatPosition)
        return nullptr;

    return openingBefore(points, beat);
}

double valueAtBeat(std::span<const AutomationPoint> points, double beatPosition) {
    if (points.empty())
        return 0.5;

    // Before first point
    if (beatPosition <= points.front().beatPosition)
        return points.front().value;

    // After last point
    if (beatPosition >= points.back().beatPosition)
        return points.back().value;

    // Found by search: an unrolled looping clip is thousands of points (#2118).
    const auto* opener = openingBefore(points, beatPosition);
    if (opener == nullptr)
        return 0.5;

    const auto& p1 = *opener;
    const auto& p2 = *(opener + 1);

    const double duration = p2.beatPosition - p1.beatPosition;
    if (duration <= 0.0)
        return p1.value;

    const double t = (beatPosition - p1.beatPosition) / duration;

    switch (p1.curveType) {
        case CurveInterpolation::Linear:
            return evalLinearSegment(t, p1, p2);

        case CurveInterpolation::Bezier:
            return interpolateBezier(t, p1, p2);

        case CurveInterpolation::Step:
            return p1.value;  // Hold until next point

        case CurveInterpolation::HardCorner: {
            const auto corner = hardCornerOf(p1, p2);
            if (!corner.has_value())
                return p1.value;

            const double apexT = (corner->beat - p1.beatPosition) / duration;
            if (t < apexT)
                return p1.value + (t / apexT) * (corner->value - p1.value);
            return corner->value + ((t - apexT) / (1.0 - apexT)) * (p2.value - corner->value);
        }
    }

    return 0.5;
}

double laneValueAtBeat(bool isAbsolute, std::span<const AutomationPoint> absolutePoints,
                       std::span<const int> clipIds,
                       const std::function<const AutomationClip*(int)>& getClip,
                       double beatPosition) {
    if (isAbsolute)
        return valueAtBeat(absolutePoints, beatPosition);

    for (const auto clipId : clipIds) {
        const auto* clip = getClip(clipId);
        if (clip && clip->containsBeat(beatPosition))
            return valueAtBeat(clip->points, clip->getLocalBeat(beatPosition));
    }

    // Gap between clips: hold the previous clip's final value, or before the first clip its
    // initial value.
    const AutomationClip* prevClip = nullptr;  // greatest end <= beat
    const AutomationClip* nextClip = nullptr;  // smallest start > beat
    for (const auto clipId : clipIds) {
        const auto* clip = getClip(clipId);
        if (!clip)
            continue;
        if (clip->getEndBeats() <= beatPosition) {
            if (!prevClip || clip->getEndBeats() > prevClip->getEndBeats())
                prevClip = clip;
        } else if (clip->startBeats > beatPosition) {
            if (!nextClip || clip->startBeats < nextClip->startBeats)
                nextClip = clip;
        }
    }
    if (prevClip)
        return valueAtBeat(prevClip->points, prevClip->getEndLocalBeat());
    if (nextClip)
        return valueAtBeat(nextClip->points, 0.0);

    return 0.5;
}

}  // namespace magda::sdk::automation
