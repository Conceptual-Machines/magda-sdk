#include "magda/sdk/curve/ModCurve.hpp"

#include <algorithm>
#include <cmath>

#include "magda/sdk/curve/CurveMath.hpp"

namespace magda::sdk::modcurve {

namespace {

constexpr float kPi = 3.14159265359f;

constexpr int kStepCurveType = static_cast<int>(CurveInterpolation::Step);
constexpr int kHardCornerCurveType = static_cast<int>(CurveInterpolation::HardCorner);

/// Below this a handle was never dragged and the run is shaped by tension alone.
constexpr float kHandleEpsilon = 0.000001f;

bool hasDraggedHandle(const CurvePointData& from, const CurvePointData& to) {
    return to.phase > from.phase &&
           (std::abs(from.outHandleX) > kHandleEpsilon ||
            std::abs(from.outHandleY) > kHandleEpsilon || std::abs(to.inHandleX) > kHandleEpsilon ||
            std::abs(to.inHandleY) > kHandleEpsilon);
}

}  // namespace

float waveform(LFOWaveform wave, float phase) {
    switch (wave) {
        case LFOWaveform::Sine:
            return (std::sin(2.0f * kPi * phase) + 1.0f) * 0.5f;
        case LFOWaveform::Triangle:
            return (phase < 0.5f) ? phase * 2.0f : 2.0f - phase * 2.0f;
        case LFOWaveform::Square:
            return phase < 0.5f ? 1.0f : 0.0f;
        case LFOWaveform::Saw:
            return phase;
        case LFOWaveform::ReverseSaw:
            return 1.0f - phase;
        case LFOWaveform::Custom:
            // The curve editor opens on a triangle.
            return (phase < 0.5f) ? phase * 2.0f : 2.0f - phase * 2.0f;
    }
    return 0.5f;
}

float preset(CurvePreset shape, float phase) {
    switch (shape) {
        case CurvePreset::Triangle:
            return (phase < 0.5f) ? phase * 2.0f : 2.0f - phase * 2.0f;
        case CurvePreset::Sine:
            return (std::sin(2.0f * kPi * phase) + 1.0f) * 0.5f;
        case CurvePreset::RampUp:
            return phase;
        case CurvePreset::RampDown:
            return 1.0f - phase;
        case CurvePreset::SCurve:
            return phase * phase * (3.0f - 2.0f * phase);
        case CurvePreset::Exponential:
            return (std::exp(phase * 3.0f) - 1.0f) / (std::exp(3.0f) - 1.0f);
        case CurvePreset::Logarithmic:
            return std::log(1.0f + phase * (std::exp(1.0f) - 1.0f));
        case CurvePreset::Custom:
            break;
    }
    return phase;
}

namespace {

/// points() in @p T: float for a modulator, double for timing.
template <typename T, typename Segment>
T readPoints(std::span<const CurvePointData> curve, T phase, Segment segment) {
    if (curve.empty())
        return T(0.5);
    if (curve.size() == 1)
        return curve[0].value;

    // Past the last point the run closes the cycle, back to the first.
    const CurvePointData* from = nullptr;
    const CurvePointData* to = nullptr;

    for (std::size_t i = 0; i < curve.size(); ++i) {
        if (static_cast<T>(curve[i].phase) > phase) {
            from = i == 0 ? &curve.back() : &curve[i - 1];
            to = i == 0 ? &curve.front() : &curve[i];
            break;
        }
    }

    if (from == nullptr) {
        from = &curve.back();
        to = &curve.front();
    }

    // A step holds until the next point.
    if (from->curveType == kStepCurveType)
        return from->value;

    const T fromPhase = from->phase;
    const T toPhase = to->phase;
    const T fromValue = from->value;
    const T toValue = to->value;

    T span = T(0);
    T local = T(0);

    if (toPhase < fromPhase) {
        // The wrapping run.
        span = (T(1) - fromPhase) + toPhase;
        local = phase >= fromPhase ? phase - fromPhase : (T(1) - fromPhase) + phase;
    } else {
        span = toPhase - fromPhase;
        local = phase - fromPhase;
    }

    const T t = std::clamp(span > T(0.0001) ? local / span : T(0), T(0), T(1));
    const T tension = from->tension;

    const auto applyTension = [tension](T input) {
        if (std::abs(tension) < T(0.001))
            return input;
        if (tension > T(0))
            return std::pow(input, T(1) + tension * T(2));
        return T(1) - std::pow(T(1) - input, T(1) - tension * T(2));
    };

    if (from->curveType == kHardCornerCurveType) {
        // Two straight runs; the corner is where the handle was dragged, else the midpoint.
        T cornerT = T(0.5);
        T cornerValue = fromValue + applyTension(T(0.5)) * (toValue - fromValue);

        if (hasDraggedHandle(*from, *to)) {
            const T cornerPhase =
                std::clamp(fromPhase + static_cast<T>(from->outHandleX), fromPhase, toPhase);
            const T raw = (toPhase - fromPhase > T(0.0001))
                              ? ((cornerPhase - fromPhase) / (toPhase - fromPhase))
                              : T(0.5);
            cornerT = std::clamp(raw, T(0.001), T(0.999));
            cornerValue = std::clamp(fromValue + static_cast<T>(from->outHandleY), T(0), T(1));
        }

        if (t <= cornerT)
            return fromValue + (t / cornerT) * (cornerValue - fromValue);

        return cornerValue + ((t - cornerT) / (T(1) - cornerT)) * (toValue - cornerValue);
    }

    // A bend, through the shared warp.
    return segment(fromValue, toValue, fromValue + static_cast<T>(from->outHandleY), tension,
                   hasDraggedHandle(*from, *to), t);
}

}  // namespace

float points(std::span<const CurvePointData> curve, float phase) {
    return readPoints<float>(curve, phase,
                             [](auto... args) { return curvemath::evalSegment(args...); });
}

double pointsPrecise(std::span<const CurvePointData> curve, double phase) {
    return readPoints<double>(curve, phase,
                              [](auto... args) { return curvemath::evalSegmentPrecise(args...); });
}

float shapeAt(LFOWaveform wave, CurvePreset shape, std::span<const CurvePointData> curve,
              float phase) {
    if (wave != LFOWaveform::Custom)
        return waveform(wave, phase);

    return curve.empty() ? preset(shape, phase) : points(curve, phase);
}

float endValue(LFOWaveform wave, CurvePreset shape, std::span<const CurvePointData> curve) {
    if (wave != LFOWaveform::Custom)
        return waveform(wave, 1.0f);

    return curve.empty() ? preset(shape, 1.0f) : curve.back().value;
}

}  // namespace magda::sdk::modcurve
