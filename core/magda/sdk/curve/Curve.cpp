#include "magda/sdk/curve/Curve.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <span>
#include <utility>

#include "magda/sdk/curve/AutomationCurve.hpp"
#include "magda/sdk/curve/ModCurve.hpp"
#include "magda/sdk/state/detail/Json.hpp"
#include "magda/sdk/state/detail/NumberText.hpp"

namespace magda::sdk {

namespace {

using detail::JsonValue;

constexpr int kMaxCurveJsonDepth = 8;
constexpr std::size_t kMaxPoints = std::size_t{1} << 22;

constexpr std::string_view kInterpolationNames[] = {"linear", "bezier", "step", "hardCorner"};

bool isPlusZero(double v) {
    return v == 0.0 && !std::signbit(v);
}

bool isValidInterpolation(int ordinal) {
    return ordinal >= 0 && ordinal <= static_cast<int>(CurveInterpolation::HardCorner);
}

/// The first rule @p curve breaks beyond JSON shape, shared by the reader and the writer.
std::optional<std::string> checkCurve(const Curve& curve) {
    if (const auto* phase = std::get_if<std::vector<CurvePointData>>(&curve.points)) {
        if (phase->size() > kMaxPoints)
            return "too many points";
        float previous = 0.0f;
        for (const auto& p : *phase) {
            const float numbers[] = {p.phase,     p.value,      p.tension,   p.inHandleX,
                                     p.inHandleY, p.outHandleX, p.outHandleY};
            if (!std::ranges::all_of(numbers, [](float v) { return std::isfinite(v); }))
                return "a point has a value that is not finite";
            if (p.phase < 0.0f || p.phase > 1.0f)
                return "a phase must be within [0, 1]";
            if (p.phase < previous)
                return "points must be in x order";
            previous = p.phase;
            if (!isValidInterpolation(p.curveType))
                return "a point has an unknown interpolation";
        }
        return std::nullopt;
    }

    const auto& beats = std::get<std::vector<AutomationPoint>>(curve.points);
    if (beats.size() > kMaxPoints)
        return "too many points";
    double previous = -std::numeric_limits<double>::infinity();
    for (const auto& p : beats) {
        const double numbers[] = {p.beatPosition,        p.value,          p.tension,
                                  p.inHandle.beatOffset, p.inHandle.value, p.outHandle.beatOffset,
                                  p.outHandle.value};
        if (!std::ranges::all_of(numbers, [](double v) { return std::isfinite(v); }))
            return "a point has a value that is not finite";
        if (p.beatPosition < previous)
            return "points must be in x order";
        previous = p.beatPosition;
        if (!isValidInterpolation(static_cast<int>(p.curveType)))
            return "a point has an unknown interpolation";
    }
    return std::nullopt;
}

CurveReadResult refuse(CurveStatus status, std::string message) {
    CurveReadResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

CurveReadResult invalid(std::string message) {
    return refuse(CurveStatus::Invalid, std::move(message));
}

bool onlyMembers(const JsonValue& object, std::initializer_list<std::string_view> allowed,
                 std::string& error) {
    for (const auto& [name, value] : object.object) {
        if (std::ranges::find(allowed, name) == allowed.end()) {
            error = "unknown member \"" + name + "\"";
            return false;
        }
    }
    return true;
}

bool isNumber(const JsonValue* value) {
    return value != nullptr &&
           (value->type == JsonValue::Type::Int || value->type == JsonValue::Type::Double);
}

double numberOf(const JsonValue& value) {
    return value.type == JsonValue::Type::Int ? static_cast<double>(value.integer) : value.real;
}

bool readNumber(const JsonValue& object, std::string_view key, double& out, std::string& error,
                std::optional<double> fallback = std::nullopt) {
    const auto* value = object.member(key);
    if (value == nullptr) {
        if (fallback) {
            out = *fallback;
            return true;
        }
        error = "missing \"" + std::string(key) + "\"";
        return false;
    }
    if (!isNumber(value)) {
        error = "\"" + std::string(key) + "\" must be a number";
        return false;
    }
    out = numberOf(*value);
    return true;
}

bool toFloat(double value, float& out, std::string& error) {
    out = static_cast<float>(value);
    if (!std::isfinite(value) || !std::isfinite(out)) {
        error = "a number does not fit a float";
        return false;
    }
    return true;
}

/// A handle object, or nothing for the zero handle. @p linked is read only in the beats domain.
bool readHandle(const JsonValue& point, std::string_view key, bool beats, double& x, double& y,
                bool& linked, std::string& error) {
    x = 0.0;
    y = 0.0;
    linked = true;
    const auto* handle = point.member(key);
    if (handle == nullptr)
        return true;
    if (handle->type != JsonValue::Type::Object) {
        error = "\"" + std::string(key) + "\" must be an object";
        return false;
    }
    const bool ok = beats ? onlyMembers(*handle, {"x", "y", "linked"}, error)
                          : onlyMembers(*handle, {"x", "y"}, error);
    if (!ok || !readNumber(*handle, "x", x, error) || !readNumber(*handle, "y", y, error))
        return false;
    if (const auto* flag = handle->member("linked")) {
        if (flag->type != JsonValue::Type::Bool || flag->boolean) {
            error = "\"linked\" is written only as false";
            return false;
        }
        linked = false;
    }
    if (isPlusZero(x) && isPlusZero(y) && linked) {
        error = "a zero handle is omitted";
        return false;
    }
    return true;
}

bool readInterpolation(const JsonValue& point, int& ordinal, std::string& error) {
    const auto* value = point.member("interpolation");
    if (value == nullptr || value->type != JsonValue::Type::String) {
        error = "\"interpolation\" must be a string";
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (value->string == kInterpolationNames[i]) {
            ordinal = i;
            return true;
        }
    }
    error = "unknown interpolation \"" + value->string + "\"";
    return false;
}

bool readTension(const JsonValue& point, double& tension, std::string& error) {
    if (!readNumber(point, "tension", tension, error, 0.0))
        return false;
    if (point.member("tension") != nullptr && isPlusZero(tension)) {
        error = "a zero tension is omitted";
        return false;
    }
    return true;
}

bool readPhasePoint(const JsonValue& json, CurvePointData& p, std::string& error) {
    if (json.type != JsonValue::Type::Object) {
        error = "a point must be an object";
        return false;
    }
    if (!onlyMembers(json, {"x", "y", "interpolation", "tension", "inHandle", "outHandle"}, error))
        return false;

    double x = 0.0;
    double y = 0.0;
    double tension = 0.0;
    double inX = 0.0;
    double inY = 0.0;
    double outX = 0.0;
    double outY = 0.0;
    bool linked = true;
    int ordinal = 0;
    if (!readNumber(json, "x", x, error) || !readNumber(json, "y", y, error) ||
        !readInterpolation(json, ordinal, error) || !readTension(json, tension, error) ||
        !readHandle(json, "inHandle", false, inX, inY, linked, error) ||
        !readHandle(json, "outHandle", false, outX, outY, linked, error))
        return false;

    p.curveType = ordinal;
    return toFloat(x, p.phase, error) && toFloat(y, p.value, error) &&
           toFloat(tension, p.tension, error) && toFloat(inX, p.inHandleX, error) &&
           toFloat(inY, p.inHandleY, error) && toFloat(outX, p.outHandleX, error) &&
           toFloat(outY, p.outHandleY, error);
}

bool readBeatPoint(const JsonValue& json, AutomationPoint& p, std::string& error) {
    if (json.type != JsonValue::Type::Object) {
        error = "a point must be an object";
        return false;
    }
    if (!onlyMembers(json, {"x", "y", "interpolation", "tension", "inHandle", "outHandle", "id"},
                     error))
        return false;

    int ordinal = 0;
    if (!readNumber(json, "x", p.beatPosition, error) || !readNumber(json, "y", p.value, error) ||
        !readInterpolation(json, ordinal, error) || !readTension(json, p.tension, error) ||
        !readHandle(json, "inHandle", true, p.inHandle.beatOffset, p.inHandle.value,
                    p.inHandle.linked, error) ||
        !readHandle(json, "outHandle", true, p.outHandle.beatOffset, p.outHandle.value,
                    p.outHandle.linked, error))
        return false;
    p.curveType = static_cast<CurveInterpolation>(ordinal);

    if (const auto* id = json.member("id")) {
        if (id->type != JsonValue::Type::Int || id->integer < std::numeric_limits<int>::min() ||
            id->integer > std::numeric_limits<int>::max() ||
            id->integer == kInvalidAutomationPointId) {
            error = "\"id\" must be an integer other than -1, or absent";
            return false;
        }
        p.id = static_cast<int>(id->integer);
    }
    return true;
}

void appendHandle(std::string& out, std::string_view key, std::string x, std::string y,
                  bool linked) {
    out += ", \"";
    out += key;
    out += "\": { \"x\": " + x + ", \"y\": " + y;
    if (!linked)
        out += ", \"linked\": false";
    out += " }";
}

bool isPlusZeroFloat(float v) {
    return isPlusZero(static_cast<double>(v));
}

void appendPhasePoint(std::string& out, const CurvePointData& p) {
    out += "    { \"x\": " + detail::writeFloat(p.phase) +
           ", \"y\": " + detail::writeFloat(p.value) + ", \"interpolation\": \"" +
           std::string(kInterpolationNames[p.curveType]) + "\"";
    if (!isPlusZeroFloat(p.tension))
        out += ", \"tension\": " + detail::writeFloat(p.tension);
    if (!isPlusZeroFloat(p.inHandleX) || !isPlusZeroFloat(p.inHandleY))
        appendHandle(out, "inHandle", detail::writeFloat(p.inHandleX),
                     detail::writeFloat(p.inHandleY), true);
    if (!isPlusZeroFloat(p.outHandleX) || !isPlusZeroFloat(p.outHandleY))
        appendHandle(out, "outHandle", detail::writeFloat(p.outHandleX),
                     detail::writeFloat(p.outHandleY), true);
    out += " }";
}

void appendBeatPoint(std::string& out, const AutomationPoint& p) {
    out += "    { \"x\": " + detail::writeDouble(p.beatPosition) +
           ", \"y\": " + detail::writeDouble(p.value) + ", \"interpolation\": \"" +
           std::string(kInterpolationNames[static_cast<int>(p.curveType)]) + "\"";
    if (!isPlusZero(p.tension))
        out += ", \"tension\": " + detail::writeDouble(p.tension);
    for (const auto& [key, handle] :
         {std::pair<std::string_view, const BezierHandle&>{"inHandle", p.inHandle},
          std::pair<std::string_view, const BezierHandle&>{"outHandle", p.outHandle}}) {
        if (!isPlusZero(handle.beatOffset) || !isPlusZero(handle.value) || !handle.linked)
            appendHandle(out, key, detail::writeDouble(handle.beatOffset),
                         detail::writeDouble(handle.value), handle.linked);
    }
    if (p.id != kInvalidAutomationPointId)
        out += ", \"id\": " + std::to_string(p.id);
    out += " }";
}

}  // namespace

CurveReadResult readCurve(std::string_view json) {
    std::string error;
    const auto parsed = detail::parseJson(json, error, kMaxCurveJsonDepth);
    if (!parsed)
        return refuse(CurveStatus::NotJson, error);

    const auto* format = parsed->member("format");
    if (parsed->type != JsonValue::Type::Object || format == nullptr ||
        format->type != JsonValue::Type::String || format->string != kCurveFormat)
        return refuse(CurveStatus::NotACurve, "not a magda.curve document");

    const auto* version = parsed->member("version");
    if (version == nullptr || version->type != JsonValue::Type::Int)
        return invalid("\"version\" must be an integer");
    if (version->integer < 1)
        return refuse(CurveStatus::UnsupportedVersion, "version is older than this build reads");
    if (version->integer > kCurveVersion)
        return refuse(CurveStatus::FutureVersion, "version is newer than this build reads");

    if (!onlyMembers(*parsed, {"format", "version", "domain", "range", "points"}, error))
        return invalid(error);

    const auto* domain = parsed->member("domain");
    if (domain == nullptr || domain->type != JsonValue::Type::String ||
        (domain->string != "phase" && domain->string != "beats"))
        return invalid("\"domain\" must be \"phase\" or \"beats\"");
    const bool beats = domain->string == "beats";

    const auto* range = parsed->member("range");
    if (range == nullptr || range->type != JsonValue::Type::Object ||
        !onlyMembers(*range, {"min", "max"}, error))
        return invalid(error.empty() ? "\"range\" must be an object" : "range: " + error);
    double lo = 0.0;
    double hi = 0.0;
    if (!readNumber(*range, "min", lo, error) || !readNumber(*range, "max", hi, error))
        return invalid("range: " + error);
    if (lo != 0.0 || hi != 1.0)
        return invalid("\"range\" must be 0 to 1 in version 1");

    const auto* points = parsed->member("points");
    if (points == nullptr || points->type != JsonValue::Type::Array)
        return invalid("\"points\" must be an array");

    Curve curve;
    if (beats) {
        std::vector<AutomationPoint> read;
        read.reserve(points->array.size());
        for (const auto& entry : points->array) {
            AutomationPoint p;
            if (!readBeatPoint(entry, p, error))
                return invalid("point " + std::to_string(read.size()) + ": " + error);
            read.push_back(p);
        }
        curve.points = std::move(read);
    } else {
        std::vector<CurvePointData> read;
        read.reserve(points->array.size());
        for (const auto& entry : points->array) {
            CurvePointData p;
            if (!readPhasePoint(entry, p, error))
                return invalid("point " + std::to_string(read.size()) + ": " + error);
            read.push_back(p);
        }
        curve.points = std::move(read);
    }

    if (const auto broken = checkCurve(curve))
        return invalid(*broken);

    CurveReadResult result;
    result.status = CurveStatus::Ok;
    result.curve = std::move(curve);
    return result;
}

std::optional<std::string> writeCurve(const Curve& curve, std::string& error) {
    if (const auto broken = checkCurve(curve)) {
        error = *broken;
        return std::nullopt;
    }

    std::string out = "{\n  \"format\": \"" + std::string(kCurveFormat) +
                      "\",\n  \"version\": " + std::to_string(kCurveVersion) +
                      ",\n  \"domain\": \"" +
                      (curve.domain() == CurveDomain::Phase ? "phase" : "beats") +
                      "\",\n  \"range\": { \"min\": 0.0, \"max\": 1.0 },\n  \"points\": [";

    bool first = true;
    const auto separator = [&] {
        out += first ? "\n" : ",\n";
        first = false;
    };
    if (const auto* phase = std::get_if<std::vector<CurvePointData>>(&curve.points)) {
        for (const auto& p : *phase) {
            separator();
            appendPhasePoint(out, p);
        }
    } else {
        for (const auto& p : std::get<std::vector<AutomationPoint>>(curve.points)) {
            separator();
            appendBeatPoint(out, p);
        }
    }
    out += first ? "]\n}\n" : "\n  ]\n}\n";
    return out;
}

bool isFutureCurve(std::string_view json) {
    std::string error;
    const auto parsed = detail::parseJson(json, error, kMaxCurveJsonDepth);
    if (!parsed || parsed->type != JsonValue::Type::Object)
        return false;
    const auto* format = parsed->member("format");
    const auto* version = parsed->member("version");
    return format != nullptr && format->type == JsonValue::Type::String &&
           format->string == kCurveFormat && version != nullptr &&
           version->type == JsonValue::Type::Int && version->integer > kCurveVersion;
}

double evaluateCurve(const Curve& curve, double x) {
    if (const auto* phase = std::get_if<std::vector<CurvePointData>>(&curve.points))
        return modcurve::points(*phase, static_cast<float>(x));
    return automation::valueAtBeat(std::get<std::vector<AutomationPoint>>(curve.points), x);
}

}  // namespace magda::sdk
