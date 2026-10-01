#pragma once

#include <cstddef>
#include <vector>

namespace magda::sdk {

/**
 * @brief Ramer-Douglas-Peucker simplification of a recorded automation polyline.
 *
 * Keeps the fewest points that preserve the shape within a tolerance, measured as the vertical
 * distance from a candidate to the straight line between the range endpoints: how far linear
 * interpolation would stray from the recorded value. First and last points are always kept.
 */
class AutomationCurveSimplifier {
  public:
    struct Point {
        double beatPosition;
        double value;
    };

    /**
     * @brief The indices of the points to keep, ascending.
     *
     * @param points   Sorted by beatPosition, values normalised to [0, 1].
     * @param epsilon  Tolerance in value units; typically 0.005 to 0.02.
     */
    static std::vector<std::size_t> simplify(const std::vector<Point>& points, double epsilon);
};

}  // namespace magda::sdk
