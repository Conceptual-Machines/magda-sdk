#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace magda::sdk::detail {

/// A parsed JSON value. Objects keep their member order.
struct JsonValue {
    enum class Type { Null, Bool, Int, Double, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    std::int64_t integer = 0;
    double real = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    const JsonValue* member(std::string_view key) const;
};

/**
 * @brief Parse RFC 8259 JSON, strictly.
 *
 * Refuses trailing content, duplicate object keys, control characters in strings, invalid UTF-8,
 * lone surrogate escapes, numbers outside the grammar or outside double range, and nesting deeper
 * than @p maxDepth. A number with no fraction or exponent that fits int64 is Int, otherwise Double.
 */
std::optional<JsonValue> parseJson(std::string_view text, std::string& error, int maxDepth = 128);

/// Append @p text as a quoted JSON string. False, appending nothing, for invalid UTF-8.
bool appendJsonString(std::string& out, std::string_view text);

/**
 * @brief Append @p value as JSON, objects in member order, doubles as the shortest text that reads
 *        back (always with a '.' or exponent, so Int and Double survive).
 *
 * Compact when @p indent is negative, else pretty-printed with two spaces per level starting at
 * @p indent. False, saying why in @p error, for invalid UTF-8 or a non-finite double.
 */
bool appendJson(std::string& out, const JsonValue& value, int indent, std::string& error);

bool isValidUtf8(std::string_view text);

}  // namespace magda::sdk::detail
