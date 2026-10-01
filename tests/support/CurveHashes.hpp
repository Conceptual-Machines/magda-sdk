#pragma once

// The sample grids of CurveCorpus.hpp folded into one hash per curve through the SDK evaluators.
// Needs CurveCorpus.hpp's types visible in namespace magda.

#include <map>
#include <string>
#include <support/CurveCorpus.hpp>

#include "magda/sdk/curve/AutomationCurve.hpp"
#include "magda/sdk/curve/AutomationCurveSimplifier.hpp"
#include "magda/sdk/curve/ModCurve.hpp"

namespace magda::curvecorpus {

using Hashes = std::map<std::string, std::uint64_t>;

inline constexpr sdk::LFOWaveform kWaves[] = {
    sdk::LFOWaveform::Sine, sdk::LFOWaveform::Triangle,   sdk::LFOWaveform::Square,
    sdk::LFOWaveform::Saw,  sdk::LFOWaveform::ReverseSaw, sdk::LFOWaveform::Custom};
inline constexpr sdk::CurvePreset kPresets[] = {
    sdk::CurvePreset::Triangle,    sdk::CurvePreset::Sine,   sdk::CurvePreset::RampUp,
    sdk::CurvePreset::RampDown,    sdk::CurvePreset::SCurve, sdk::CurvePreset::Exponential,
    sdk::CurvePreset::Logarithmic, sdk::CurvePreset::Custom};

inline void hashBuiltIns(Hashes& out) {
    for (int w = 0; w < 6; ++w) {
        Fnv h;
        for (int i = 0; i <= 1024; ++i)
            h.add(sdk::modcurve::waveform(kWaves[w], static_cast<float>(i) / 1024.0f));
        out["waveform/" + std::to_string(w)] = h.h;
    }
    for (int p = 0; p < 8; ++p) {
        Fnv h;
        for (int i = 0; i <= 1024; ++i)
            h.add(sdk::modcurve::preset(kPresets[p], static_cast<float>(i) / 1024.0f));
        out["preset/" + std::to_string(p)] = h.h;
    }
}

inline void hashPhaseCase(Hashes& out, const PhaseCase& c) {
    Fnv h;
    for (const float ph : phaseSamples(c.points))
        h.add(sdk::modcurve::points(c.points, ph));
    out["phase/" + c.name + "/points"] = h.h;

    Fnv s;
    for (int w = 0; w < 6; ++w) {
        for (int p = 0; p < 8; p += 3) {
            s.add(sdk::modcurve::endValue(kWaves[w], kPresets[p], c.points));
            for (const float ph : phaseSamples(c.points))
                s.add(sdk::modcurve::shapeAt(kWaves[w], kPresets[p], c.points, ph));
        }
    }
    out["phase/" + c.name + "/shapeAt"] = s.h;
}

inline void hashBeatCase(Hashes& out, const BeatCase& c) {
    Fnv v;
    Fnv k;
    Fnv o;
    for (const double b : beatSamples(c.points)) {
        v.add(sdk::automation::valueAtBeat(c.points, b));
        const auto* op = sdk::automation::segmentOpening(c.points, b);
        o.add(static_cast<std::uint64_t>(op ? (op - c.points.data()) : -1));
    }
    for (std::size_t i = 0; i + 1 < c.points.size(); ++i) {
        const auto corner = sdk::automation::hardCornerOf(c.points[i], c.points[i + 1]);
        k.add(static_cast<std::uint64_t>(corner.has_value()));
        if (corner) {
            k.add(corner->beat);
            k.add(corner->value);
        }
    }
    out["beat/" + c.name + "/value"] = v.h;
    out["beat/" + c.name + "/corner"] = k.h;
    out["beat/" + c.name + "/opening"] = o.h;

    Fnv lane;
    for (const double b : beatSamples(c.points))
        lane.add(sdk::automation::laneValueAtBeat(
            true, c.points, {}, [](int) -> const sdk::AutomationClip* { return nullptr; }, b));
    out["laneabs/" + c.name] = lane.h;
}

inline void hashLaneCase(Hashes& out, const LaneCase& c) {
    std::vector<sdk::AutomationClip> clips;
    std::vector<int> ids;
    for (const auto& s : c.clips) {
        sdk::AutomationClip clip;
        clip.startBeats = s.startBeats;
        clip.lengthBeats = s.lengthBeats;
        clip.looping = s.looping;
        clip.loopLengthBeats = s.loopLengthBeats;
        clip.points = s.points;
        clips.push_back(clip);
        ids.push_back(s.id);
    }
    const auto getClip = [&](int id) -> const sdk::AutomationClip* {
        for (std::size_t i = 0; i < ids.size(); ++i)
            if (ids[i] == id)
                return &clips[i];
        return nullptr;
    };
    Fnv h;
    for (const double b : laneSamples())
        h.add(sdk::automation::laneValueAtBeat(false, {}, ids, getClip, b));
    out["lane/" + c.name] = h.h;
}

inline void hashSimplifyCase(Hashes& out, const SimplifyCase& c) {
    std::vector<sdk::AutomationCurveSimplifier::Point> pts;
    for (const auto& [b, v] : c.points)
        pts.push_back({b, v});
    Fnv h;
    for (const auto idx : sdk::AutomationCurveSimplifier::simplify(pts, c.epsilon))
        h.add(static_cast<std::uint64_t>(idx));
    out["simplify/" + c.name] = h.h;
}

}  // namespace magda::curvecorpus
