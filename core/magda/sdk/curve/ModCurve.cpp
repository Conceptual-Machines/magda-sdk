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

float points(std::span<const CurvePointData> curve, float phase) {
    if (curve.empty())
        return 0.5f;
    if (curve.size() == 1)
        return curve[0].value;

    // Past the last point the run closes the cycle, back to the first.
    const CurvePointData* from = nullptr;
    const CurvePointData* to = nullptr;

    for (std::size_t i = 0; i < curve.size(); ++i) {
        if (curve[i].phase > phase) {
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

    float span = 0.0f;
    float local = 0.0f;

    if (to->phase < from->phase) {
        // The wrapping run.
        span = (1.0f - from->phase) + to->phase;
        local = phase >= from->phase ? phase - from->phase : (1.0f - from->phase) + phase;
    } else {
        span = to->phase - from->phase;
        local = phase - from->phase;
    }

    const float t = std::clamp(span > 0.0001f ? local / span : 0.0f, 0.0f, 1.0f);
    const float tension = from->tension;

    const auto applyTension = [tension](float input) {
        if (std::abs(tension) < 0.001f)
            return input;
        if (tension > 0.0f)
            return std::pow(input, 1.0f + tension * 2.0f);
        return 1.0f - std::pow(1.0f - input, 1.0f - tension * 2.0f);
    };

    if (from->curveType == kHardCornerCurveType) {
        // Two straight runs; the corner is where the handle was dragged, else the midpoint.
        float cornerT = 0.5f;
        float cornerValue = from->value + applyTension(0.5f) * (to->value - from->value);

        if (hasDraggedHandle(*from, *to)) {
            const float cornerPhase =
                std::clamp(from->phase + from->outHandleX, from->phase, to->phase);
            const float raw = (to->phase - from->phase > 0.0001f)
                                  ? ((cornerPhase - from->phase) / (to->phase - from->phase))
                                  : 0.5f;
            cornerT = std::clamp(raw, 0.001f, 0.999f);
            cornerValue = std::clamp(from->value + from->outHandleY, 0.0f, 1.0f);
        }

        if (t <= cornerT)
            return from->value + (t / cornerT) * (cornerValue - from->value);

        return cornerValue + ((t - cornerT) / (1.0f - cornerT)) * (to->value - cornerValue);
    }

    // A bend, through the shared warp.
    return curvemath::evalSegment(from->value, to->value, from->value + from->outHandleY, tension,
                                  hasDraggedHandle(*from, *to), t);
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
