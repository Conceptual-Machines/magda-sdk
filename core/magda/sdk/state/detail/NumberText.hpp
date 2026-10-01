#pragma once

#include <string>
#include <string_view>

namespace magda::sdk::detail {

/**
 * @brief Read a double the way juce::String::getDoubleValue does.
 *
 * Leading blanks, a sign, digits, a point, an exponent, "nan" and "inf"; anything after the
 * number is ignored and no number reads as 0. The result may be non-finite.
 */
double readLenientDouble(std::string_view text);

/// Convert a token already known to be a JSON number. Locale independent.
double parseNumberToken(std::string_view token);

/// The shortest text that reads back as @p value, always with a '.' or an exponent.
std::string writeDouble(double value);

}  // namespace magda::sdk::detail
