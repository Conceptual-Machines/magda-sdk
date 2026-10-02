#pragma once

// Deterministic curve corpus shared by the SDK tests and magda-core's serialization test.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <magda/sdk/curve/CurveTypes.hpp>
#include <span>
#include <string>
#include <vector>

namespace magda::curvecorpus {

using sdk::AutomationPoint;
using sdk::BezierHandle;
using sdk::CurvePointData;
using AutomationCurveType = sdk::CurveInterpolation;

struct Rng {
    std::uint64_t state;

    std::uint64_t next() {
        state += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    double unit() {
        return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0);
    }

    double range(double lo, double hi) {
        return lo + unit() * (hi - lo);
    }

    int below(int n) {
        return static_cast<int>(next() % static_cast<std::uint64_t>(n));
    }

    bool chance(double p) {
        return unit() < p;
    }
};

struct Fnv {
    std::uint64_t h = 1469598103934665603ull;

    void add(std::uint64_t word) {
        for (int i = 0; i < 8; ++i) {
            h ^= (word >> (8 * i)) & 0xFFu;
            h *= 1099511628211ull;
        }
    }

    void add(float v) {
        add(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(v)));
    }

    void add(double v) {
        add(std::bit_cast<std::uint64_t>(v));
    }
};

struct PhaseCase {
    std::string name;
    std::vector<CurvePointData> points;
};

struct BeatCase {
    std::string name;
    std::vector<AutomationPoint> points;
};

struct ClipSpec {
    int id = 0;
    double startBeats = 0.0;
    double lengthBeats = 4.0;
    bool looping = false;
    double loopLengthBeats = 4.0;
    std::vector<AutomationPoint> points;
};

struct LaneCase {
    std::string name;
    std::vector<ClipSpec> clips;
};

struct SimplifyCase {
    std::string name;
    std::vector<std::pair<double, double>> points;
    double epsilon = 0.01;
};

inline CurvePointData cp(float phase, float value, int curveType = 0, float tension = 0.0f,
                         float inX = 0.0f, float inY = 0.0f, float outX = 0.0f, float outY = 0.0f) {
    CurvePointData p;
    p.phase = phase;
    p.value = value;
    p.curveType = curveType;
    p.tension = tension;
    p.inHandleX = inX;
    p.inHandleY = inY;
    p.outHandleX = outX;
    p.outHandleY = outY;
    return p;
}

inline AutomationPoint ap(int id, double beat, double value,
                          AutomationCurveType type = AutomationCurveType::Linear,
                          double tension = 0.0, BezierHandle in = {}, BezierHandle out = {}) {
    AutomationPoint p;
    p.id = id;
    p.beatPosition = beat;
    p.value = value;
    p.curveType = type;
    p.tension = tension;
    p.inHandle = in;
    p.outHandle = out;
    return p;
}

inline BezierHandle bh(double beatOffset, double value, bool linked = true) {
    BezierHandle h;
    h.beatOffset = beatOffset;
    h.value = value;
    h.linked = linked;
    return h;
}

inline std::vector<PhaseCase> phaseCases() {
    std::vector<PhaseCase> out;
    out.push_back({"empty", {}});
    out.push_back({"single", {cp(0.3f, 0.7f)}});
    out.push_back({"two_linear", {cp(0.0f, 0.0f), cp(1.0f, 1.0f)}});
    out.push_back({"triangle", {cp(0.0f, 0.0f), cp(0.5f, 1.0f), cp(1.0f, 0.0f)}});
    out.push_back({"wrap_late_first", {cp(0.2f, 0.9f), cp(0.6f, 0.1f), cp(0.85f, 0.5f)}});
    out.push_back({"tension_pos", {cp(0.0f, 0.1f, 0, 2.5f), cp(0.7f, 0.9f, 0, -1.25f)}});
    out.push_back(
        {"tension_neg",
         {cp(0.1f, 0.8f, 0, -3.0f), cp(0.5f, 0.2f, 0, 3.0f), cp(0.9f, 0.6f, 0, 0.0005f)}});
    out.push_back(
        {"step", {cp(0.0f, 0.2f, 2), cp(0.25f, 0.8f, 2), cp(0.5f, 0.4f, 2), cp(0.75f, 1.0f, 2)}});
    out.push_back(
        {"hard_corner_default", {cp(0.0f, 0.0f, 3), cp(0.5f, 1.0f, 3, 1.5f), cp(1.0f, 0.2f, 3)}});
    out.push_back({"hard_corner_dragged",
                   {cp(0.0f, 0.1f, 3, 0.0f, 0, 0, 0.1f, 0.6f),
                    cp(0.5f, 0.9f, 3, 0.0f, -0.05f, 0.1f, 0.3f, -0.8f),
                    cp(0.9f, 0.3f, 3, 0.0f, 0, 0, 0.05f, 2.0f)}});
    out.push_back(
        {"bend_handle",
         {cp(0.0f, 0.0f, 0, 0.0f, 0, 0, 0.2f, 0.9f), cp(1.0f, 1.0f, 0, 0.0f, -0.2f, -0.9f)}});
    out.push_back({"bend_in_handle_only", {cp(0.0f, 0.2f), cp(0.6f, 0.8f, 0, 0.0f, -0.1f, 0.4f)}});
    out.push_back({"bezier_type_handle",
                   {cp(0.1f, 0.3f, 1, 0.5f, 0, 0, 0.1f, -0.7f), cp(0.4f, 0.8f, 1, -0.5f),
                    cp(0.95f, 0.1f, 1)}});
    out.push_back({"sidechain_duck",
                   {cp(0.0f, 1.0f, 0, 0.0f), cp(0.03f, 0.05f, 0, 1.2f), cp(0.45f, 0.1f, 0, -0.8f),
                    cp(0.85f, 1.0f, 0, 0.0f, 0, 0, 0.1f, 0.0f)}});
    out.push_back(
        {"coincident_phases", {cp(0.0f, 0.0f), cp(0.5f, 1.0f), cp(0.5f, 0.2f), cp(1.0f, 0.6f)}});
    out.push_back(
        {"out_of_unit_handles",
         {cp(0.0f, 0.5f, 0, 0.0f, 0, 0, 0.3f, 3.0f), cp(1.0f, 0.5f, 0, 0.0f, -0.3f, -3.0f)}});

    Rng rng{0xC0FFEE1234ull};
    for (int c = 0; c < 48; ++c) {
        const int n = 1 + rng.below(12);
        std::vector<float> phases;
        for (int i = 0; i < n; ++i)
            phases.push_back(static_cast<float>(rng.unit()));
        std::sort(phases.begin(), phases.end());
        PhaseCase pc;
        pc.name = "random_" + std::to_string(c);
        for (int i = 0; i < n; ++i) {
            CurvePointData p;
            p.phase = phases[static_cast<std::size_t>(i)];
            p.value = static_cast<float>(rng.unit());
            p.curveType = rng.below(4);
            p.tension = rng.chance(0.5) ? static_cast<float>(rng.range(-3.0, 3.0)) : 0.0f;
            if (rng.chance(0.4)) {
                p.outHandleX = static_cast<float>(rng.range(0.0, 0.3));
                p.outHandleY = static_cast<float>(rng.range(-0.5, 0.5));
                p.inHandleX = static_cast<float>(rng.range(-0.3, 0.0));
                p.inHandleY = static_cast<float>(rng.range(-0.5, 0.5));
            }
            pc.points.push_back(p);
        }
        out.push_back(std::move(pc));
    }
    return out;
}

inline std::vector<BeatCase> beatCases() {
    using T = AutomationCurveType;
    std::vector<BeatCase> out;
    out.push_back({"empty", {}});
    out.push_back({"single", {ap(1, 2.0, 0.25)}});
    out.push_back({"two_linear", {ap(1, 0.0, 0.0), ap(2, 4.0, 1.0)}});
    out.push_back({"tension",
                   {ap(1, 0.0, 0.1, T::Linear, 0.8), ap(2, 4.0, 0.9, T::Linear, -0.6),
                    ap(3, 8.0, 0.2, T::Linear, 0.0004), ap(4, 12.0, 0.7)}});
    out.push_back({"step",
                   {ap(1, 0.0, 0.3, T::Step), ap(2, 1.0, 0.9, T::Step), ap(3, 2.5, 0.1, T::Step),
                    ap(4, 4.0, 0.6)}});
    out.push_back(
        {"hard_corner_default",
         {ap(1, 0.0, 0.0, T::HardCorner), ap(2, 4.0, 1.0, T::HardCorner), ap(3, 6.0, 0.3)}});
    out.push_back({"hard_corner_dragged",
                   {ap(1, 0.0, 0.1, T::HardCorner, 0.0, {}, bh(1.0, 0.7)),
                    ap(2, 4.0, 0.9, T::HardCorner, 0.0, bh(-0.5, 0.1), bh(3.9999, -0.5, false)),
                    ap(3, 8.0, 0.4)}});
    out.push_back({"hard_corner_apex_clamped",
                   {ap(1, 0.0, 0.1, T::HardCorner, 0.0, {}, bh(-2.0, 0.7)),
                    ap(2, 4.0, 0.9, T::HardCorner, 0.0, {}, bh(9.0, -0.5)), ap(3, 8.0, 0.4)}});
    out.push_back({"bezier",
                   {ap(1, 0.0, 0.0, T::Bezier, 0.0, {}, bh(1.0, 0.8)),
                    ap(2, 4.0, 1.0, T::Bezier, 0.0, bh(-1.0, -0.2), bh(0.5, 0.1, false)),
                    ap(3, 8.0, 0.3, T::Bezier, 0.0, bh(-2.5, 0.4), {}), ap(4, 9.0, 0.6)}});
    out.push_back({"bezier_overshoot",
                   {ap(1, 0.0, 0.2, T::Bezier, 0.0, {}, bh(6.0, 1.5)),
                    ap(2, 2.0, 0.8, T::Bezier, 0.0, bh(-6.0, -1.5))}});
    out.push_back({"linear_shaper",
                   {ap(1, 0.0, 0.0, T::Linear, 0.0, {}, bh(1.0, 0.9)),
                    ap(2, 4.0, 1.0, T::Linear, 0.0, bh(-1.0, -0.9))}});
    out.push_back({"linear_in_handle_only",
                   {ap(1, 1.0, 0.2), ap(2, 5.0, 0.7, T::Linear, 0.3, bh(-1.0, 0.4))}});
    out.push_back(
        {"coincident_beats", {ap(1, 0.0, 0.0), ap(2, 2.0, 1.0), ap(3, 2.0, 0.2), ap(4, 4.0, 0.6)}});
    out.push_back({"offset_start", {ap(7, 16.0, 0.4, T::Linear, 0.5), ap(9, 20.0, 0.9)}});

    Rng rng{0xBEA7ull};
    for (int c = 0; c < 48; ++c) {
        const int n = 1 + rng.below(16);
        BeatCase bc;
        bc.name = "random_" + std::to_string(c);
        double beat = rng.range(0.0, 8.0);
        for (int i = 0; i < n; ++i) {
            AutomationPoint p;
            p.id = 100 + i;
            p.beatPosition = beat;
            beat += rng.chance(0.1) ? 0.0 : rng.range(0.01, 4.0);
            p.value = rng.unit();
            p.curveType = static_cast<AutomationCurveType>(rng.below(4));
            p.tension = rng.chance(0.5) ? rng.range(-1.0, 1.0) : 0.0;
            if (rng.chance(0.4)) {
                p.outHandle = bh(rng.range(0.0, 2.0), rng.range(-0.5, 0.5), rng.chance(0.5));
                p.inHandle = bh(rng.range(-2.0, 0.0), rng.range(-0.5, 0.5), rng.chance(0.5));
            }
            bc.points.push_back(p);
        }
        out.push_back(std::move(bc));
    }
    return out;
}

inline std::vector<float> phaseSamples(const std::vector<CurvePointData>& points) {
    std::vector<float> out;
    for (int i = 0; i <= 1024; ++i)
        out.push_back(static_cast<float>(i) / 1024.0f);
    for (const auto& p : points)
        for (const float d : {-1.0e-4f, -1.0e-6f, 0.0f, 1.0e-6f, 1.0e-4f})
            out.push_back(std::clamp(p.phase + d, 0.0f, 1.0f));
    return out;
}

inline std::vector<double> beatSamples(const std::vector<AutomationPoint>& points) {
    std::vector<double> out;
    const double first = points.empty() ? 0.0 : points.front().beatPosition - 1.0;
    const double last = points.empty() ? 8.0 : points.back().beatPosition + 1.0;
    constexpr int kSteps = 2048;
    for (int i = 0; i <= kSteps; ++i)
        out.push_back(first + (last - first) * static_cast<double>(i) / kSteps);
    for (const auto& p : points)
        for (const double d : {-1.0e-3, -1.0e-9, 0.0, 1.0e-9, 1.0e-3})
            out.push_back(p.beatPosition + d);
    return out;
}

inline std::vector<LaneCase> laneCases() {
    std::vector<LaneCase> out;
    const auto ramp = [](int base) {
        return std::vector<AutomationPoint>{ap(base, 0.0, 0.1), ap(base + 1, 2.0, 0.9),
                                            ap(base + 2, 4.0, 0.3, AutomationCurveType::Step)};
    };
    out.push_back({"no_clips", {}});
    out.push_back({"one_clip", {{1, 4.0, 4.0, false, 4.0, ramp(10)}}});
    out.push_back({"looping", {{1, 2.0, 11.0, true, 4.0, ramp(10)}}});
    out.push_back({"looping_exact_cycle", {{1, 0.0, 8.0, true, 4.0, ramp(10)}}});
    out.push_back({"gap_between",
                   {{1, 0.0, 4.0, false, 4.0, ramp(10)},
                    {2,
                     8.0,
                     4.0,
                     false,
                     4.0,
                     {ap(20, 0.0, 0.8),
                      ap(21, 4.0, 0.2, AutomationCurveType::Bezier, 0.0, {}, bh(1.0, 0.5))}}}});
    out.push_back({"overlap_first_wins",
                   {{1, 0.0, 6.0, false, 4.0, ramp(10)}, {2, 4.0, 6.0, true, 3.0, ramp(30)}}});
    out.push_back({"empty_clip", {{1, 2.0, 4.0, false, 4.0, {}}}});
    return out;
}

inline std::vector<double> laneSamples() {
    std::vector<double> out;
    for (int i = -80; i <= 400; ++i)
        out.push_back(static_cast<double>(i) * 0.1);
    for (const double b : {3.9999999999, 4.0, 4.0000000001, 7.9999999999, 8.0, 12.0})
        out.push_back(b);
    return out;
}

inline std::vector<SimplifyCase> simplifyCases() {
    std::vector<SimplifyCase> out;
    out.push_back({"empty", {}, 0.01});
    out.push_back({"one", {{0.0, 0.5}}, 0.01});
    out.push_back({"two", {{0.0, 0.5}, {1.0, 0.7}}, 0.01});
    out.push_back({"line", {{0.0, 0.0}, {1.0, 0.25}, {2.0, 0.5}, {3.0, 0.75}, {4.0, 1.0}}, 0.005});
    out.push_back({"vertical", {{0.0, 0.0}, {0.0, 0.5}, {0.0, 1.0}}, 0.01});
    Rng rng{0x51117ull};
    for (int c = 0; c < 24; ++c) {
        SimplifyCase sc;
        sc.name = "random_" + std::to_string(c);
        sc.epsilon = rng.range(0.001, 0.05);
        const int n = 3 + rng.below(200);
        double beat = 0.0;
        double v = 0.5;
        for (int i = 0; i < n; ++i) {
            sc.points.push_back({beat, v});
            beat += rng.range(0.0, 0.25);
            v = std::clamp(v + rng.range(-0.1, 0.1), 0.0, 1.0);
        }
        out.push_back(std::move(sc));
    }
    return out;
}

}  // namespace magda::curvecorpus
