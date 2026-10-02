#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "magda/sdk/curve/CurveTypes.hpp"

/**
 * @file Curve.hpp
 * @brief The curve document: one JSON format for LFO, sidechain and automation curves
 *        (docs/curve.md).
 */

namespace magda::sdk {

/// The format and version strings a curve document carries.
inline constexpr std::string_view kCurveFormat = "magda.curve";
inline constexpr int kCurveVersion = 1;

/// What the x axis measures.
enum class CurveDomain {
    /// A cycle, 0 to 1, that wraps: an LFO or a sidechain gain shape.
    Phase,
    /// Beats on the timeline: an automation lane or clip.
    Beats,
};

/// A curve in either domain; the alternative held is its domain.
struct Curve {
    std::variant<std::vector<CurvePointData>, std::vector<AutomationPoint>> points;

    CurveDomain domain() const {
        return points.index() == 0 ? CurveDomain::Phase : CurveDomain::Beats;
    }
};

enum class CurveStatus {
    Ok,
    /// Not parseable JSON.
    NotJson,
    /// JSON, but not a `magda.curve` document.
    NotACurve,
    /// A version below 1.
    UnsupportedVersion,
    /// A version newer than kCurveVersion. Never rewrite the source (see isFutureCurve).
    FutureVersion,
    /// A version this build reads, with content the format does not allow.
    Invalid,
};

struct CurveReadResult {
    CurveStatus status = CurveStatus::NotJson;
    std::string message;
    std::optional<Curve> curve;

    bool ok() const {
        return status == CurveStatus::Ok;
    }
};

/// Parse a curve document. Strict: it reads nothing it would not write.
CurveReadResult readCurve(std::string_view json);

/// The canonical JSON text of @p curve. Nullopt, with @p error set, for one the reader would
/// refuse.
std::optional<std::string> writeCurve(const Curve& curve, std::string& error);

/// Whether @p json is a `magda.curve` object whose version is newer than this build reads.
bool isFutureCurve(std::string_view json);

/// The value of @p curve at @p x: a phase through modcurve::points, a beat through
/// automation::valueAtBeat. Phase is read as a float, as the LFO does.
double evaluateCurve(const Curve& curve, double x);

}  // namespace magda::sdk
