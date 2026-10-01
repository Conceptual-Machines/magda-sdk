#include "magda/sdk/curve/AutomationCurveSimplifier.hpp"

#include <cmath>
#include <cstddef>
#include <stack>
#include <utility>

namespace magda::sdk {

namespace {

// Vertical distance from point p to the line segment (a -> b) at p's beat position.
// "Vertical" here means along the value axis only.
double verticalDistance(const AutomationCurveSimplifier::Point& p,
                        const AutomationCurveSimplifier::Point& a,
                        const AutomationCurveSimplifier::Point& b) {
    double dt = b.beatPosition - a.beatPosition;
    if (dt <= 0.0) {
        // Degenerate segment: fall back to absolute value delta.
        return std::abs(p.value - a.value);
    }
    double t = (p.beatPosition - a.beatPosition) / dt;
    double interpolated = a.value + (t * (b.value - a.value));
    return std::abs(p.value - interpolated);
}

}  // namespace

std::vector<std::size_t> AutomationCurveSimplifier::simplify(const std::vector<Point>& points,
                                                             double epsilon) {
    const std::size_t n = points.size();
    if (n <= 2) {
        std::vector<std::size_t> all;
        all.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
            all.push_back(i);
        return all;
    }

    // Iterative RDP using an explicit stack to avoid deep recursion.
    std::vector<bool> keep(n, false);
    keep.front() = true;
    keep.back() = true;

    std::stack<std::pair<std::size_t, std::size_t>> work;
    work.emplace(0, n - 1);

    while (!work.empty()) {
        auto [lo, hi] = work.top();
        work.pop();
        if (hi <= lo + 1)
            continue;

        double maxDist = 0.0;
        std::size_t maxIdx = lo;
        for (std::size_t i = lo + 1; i < hi; ++i) {
            double d = verticalDistance(points[i], points[lo], points[hi]);
            if (d > maxDist) {
                maxDist = d;
                maxIdx = i;
            }
        }

        if (maxDist > epsilon) {
            keep[maxIdx] = true;
            work.emplace(lo, maxIdx);
            work.emplace(maxIdx, hi);
        }
    }

    std::vector<std::size_t> result;
    result.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (keep[i])
            result.push_back(i);
    }
    return result;
}

}  // namespace magda::sdk
