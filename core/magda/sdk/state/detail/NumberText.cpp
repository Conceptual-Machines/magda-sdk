#include "magda/sdk/state/detail/NumberText.hpp"

#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace magda::sdk::detail {

namespace {

bool isSpace(char c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

/// The C library's decimal point may not be '.', so the text is rewritten to match it.
double toDoubleCLocale(const std::string& text) {
    const char* point = std::localeconv()->decimal_point;
    if (point == nullptr || (point[0] == '.' && point[1] == '\0'))
        return std::strtod(text.c_str(), nullptr);

    std::string local;
    for (const auto c : text) {
        if (c == '.')
            local += point;
        else
            local.push_back(c);
    }
    return std::strtod(local.c_str(), nullptr);
}

}  // namespace

double readLenientDouble(std::string_view text) {
    constexpr auto inf = std::numeric_limits<double>::infinity();
    constexpr int maxSignificantDigits = 18;

    std::size_t pos = 0;
    while (pos < text.size() && isSpace(text[pos]))
        ++pos;

    const auto at = [&](std::size_t offset) {
        return pos + offset < text.size() ? text[pos + offset] : '\0';
    };

    bool negative = false;
    std::string buffer;
    if (at(0) == '-') {
        negative = true;
        buffer.push_back('-');
        ++pos;
    } else if (at(0) == '+') {
        ++pos;
    }

    const auto lower = [&](std::size_t offset) {
        const auto c = at(offset);
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    };

    if (lower(0) == 'n')
        return lower(1) == 'a' && lower(2) == 'n' ? std::numeric_limits<double>::quiet_NaN() : 0.0;
    if (lower(0) == 'i')
        return lower(1) == 'n' && lower(2) == 'f' ? (negative ? -inf : inf) : 0.0;

    int numSigFigs = 0;
    int extraExponent = 0;
    bool decimalPointFound = false;
    bool leadingZeros = false;

    for (;; ++pos) {
        const auto c = at(0);
        if (isDigit(c)) {
            const auto digit = c - '0';
            if (decimalPointFound) {
                if (numSigFigs >= maxSignificantDigits)
                    continue;
            } else {
                if (numSigFigs >= maxSignificantDigits) {
                    ++extraExponent;
                    continue;
                }
                if (numSigFigs == 0 && digit == 0) {
                    leadingZeros = true;
                    continue;
                }
            }
            buffer.push_back(c);
            ++numSigFigs;
        } else if (!decimalPointFound && c == '.') {
            buffer.push_back('.');
            decimalPointFound = true;
        } else {
            break;
        }
    }

    if (!leadingZeros && numSigFigs == 0)
        return 0.0;

    const auto c = at(0);
    if (c == 'e' || c == 'E') {
        buffer.push_back('e');
        ++pos;
        bool exponentPositive = true;
        if (at(0) == '-') {
            exponentPositive = false;
            ++pos;
        } else if (at(0) == '+') {
            ++pos;
        }

        int exponent = 0;
        bool anyDigit = false;
        while (isDigit(at(0))) {
            const auto digit = at(0) - '0';
            if (exponent < 100000)
                exponent = exponent * 10 + digit;
            anyDigit = true;
            ++pos;
        }
        if (!anyDigit)
            buffer.pop_back();

        exponent = extraExponent + (exponentPositive ? exponent : -exponent);
        if (anyDigit) {
            if (exponent < std::numeric_limits<double>::min_exponent10 - 1)
                return negative ? -0.0 : 0.0;
            if (exponent > std::numeric_limits<double>::max_exponent10 + 1)
                return negative ? -inf : inf;
            buffer += std::to_string(exponent);
        } else if (extraExponent > 0) {
            buffer += "e" + std::to_string(extraExponent);
        }
    } else if (extraExponent > 0) {
        buffer += "e" + std::to_string(extraExponent);
    }

    return toDoubleCLocale(buffer);
}

double parseNumberToken(std::string_view token) {
    return toDoubleCLocale(std::string(token));
}

namespace {

std::string withPoint(std::string text) {
    const char* point = std::localeconv()->decimal_point;
    if (point != nullptr && !(point[0] == '.' && point[1] == '\0')) {
        const auto at = text.find(point);
        if (at != std::string::npos)
            text.replace(at, std::char_traits<char>::length(point), ".");
    }
    return text;
}

}  // namespace

std::string writeFloat(float value) {
    char buffer[64];
    std::string text;

    // The reader goes through double, so a text must survive that path; widen until it does.
    for (int precision = 1; precision <= 9; ++precision) {
        std::snprintf(buffer, sizeof buffer, "%.*g", precision, static_cast<double>(value));
        text = withPoint(buffer);
        if (static_cast<float>(toDoubleCLocale(text)) == value)
            break;
    }

    // %g turns 1000 into 1e+03; whole numbers read better as themselves.
    if (text.find('e') != std::string::npos && value == std::floor(value) &&
        std::abs(value) < 1.0e9f) {
        std::snprintf(buffer, sizeof buffer, "%.0f", static_cast<double>(value));
        text = buffer;
    }
    if (text.find_first_of(".eE") == std::string::npos)
        text += ".0";
    return text;
}

std::string writeDouble(double value) {
    char buffer[40];
    for (const int precision : {15, 16, 17}) {
        std::snprintf(buffer, sizeof buffer, "%.*g", precision, value);
        std::string text = buffer;

        const char* point = std::localeconv()->decimal_point;
        if (point != nullptr && !(point[0] == '.' && point[1] == '\0')) {
            const auto at = text.find(point);
            if (at != std::string::npos)
                text.replace(at, std::char_traits<char>::length(point), ".");
        }

        if (precision == 17 || toDoubleCLocale(text) == value) {
            if (text.find_first_of(".eE") == std::string::npos)
                text += ".0";
            return text;
        }
    }
    return "0.0";
}

}  // namespace magda::sdk::detail
