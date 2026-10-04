#pragma once

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#include "magda/sdk/state/detail/Json.hpp"

/// Golden display lists: three decimals on disk, compared with a tolerance.
namespace magda::sdk::golden_json {

/// Absorbs the three-decimal rounding and libm ulps across platforms.
inline constexpr double kTolerance = 1.5e-3;

inline std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

inline bool isNumber(const detail::JsonValue& v) {
    return v.type == detail::JsonValue::Type::Int || v.type == detail::JsonValue::Type::Double;
}

inline double numberOf(const detail::JsonValue& v) {
    return v.type == detail::JsonValue::Type::Int ? static_cast<double>(v.integer) : v.real;
}

/// Empty when equal, else the path of the first difference.
inline std::string firstDifference(const detail::JsonValue& a, const detail::JsonValue& b,
                                   const std::string& path = "$") {
    if (isNumber(a) && isNumber(b))
        return std::abs(numberOf(a) - numberOf(b)) <= kTolerance ? "" : path;
    if (a.type != b.type)
        return path;
    switch (a.type) {
        case detail::JsonValue::Type::Array:
            if (a.array.size() != b.array.size())
                return path + " (length)";
            for (std::size_t i = 0; i < a.array.size(); ++i)
                if (auto d = firstDifference(a.array[i], b.array[i],
                                             path + "[" + std::to_string(i) + "]");
                    !d.empty())
                    return d;
            return "";
        case detail::JsonValue::Type::Object:
            if (a.object.size() != b.object.size())
                return path + " (members)";
            for (std::size_t i = 0; i < a.object.size(); ++i) {
                if (a.object[i].first != b.object[i].first)
                    return path + "." + a.object[i].first;
                if (auto d = firstDifference(a.object[i].second, b.object[i].second,
                                             path + "." + a.object[i].first);
                    !d.empty())
                    return d;
            }
            return "";
        case detail::JsonValue::Type::String:
            return a.string == b.string ? "" : path;
        case detail::JsonValue::Type::Bool:
            return a.boolean == b.boolean ? "" : path;
        default:
            return "";
    }
}

}  // namespace magda::sdk::golden_json
