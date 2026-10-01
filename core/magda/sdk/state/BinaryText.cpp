#include "magda/sdk/state/BinaryText.hpp"

#include <array>

namespace magda::sdk {

namespace {

constexpr std::string_view kAlphabet =
    ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+";

constexpr std::array<std::int8_t, 256> makeDecodeTable() {
    std::array<std::int8_t, 256> table{};
    for (auto& entry : table)
        entry = -1;
    for (std::size_t i = 0; i < kAlphabet.size(); ++i)
        table[static_cast<unsigned char>(kAlphabet[i])] = static_cast<std::int8_t>(i);
    return table;
}

constexpr auto kDecodeTable = makeDecodeTable();

bool refuse(std::string* error, const char* message) {
    if (error != nullptr)
        *error = message;
    return false;
}

}  // namespace

std::string encodeBinaryText(std::span<const std::uint8_t> bytes) {
    const auto numBits = bytes.size() * 8;
    const auto numChars = (numBits + 5) / 6;

    std::string text = std::to_string(bytes.size());
    text.push_back('.');
    text.reserve(text.size() + numChars);

    for (std::size_t i = 0; i < numChars; ++i) {
        unsigned value = 0;
        for (std::size_t j = 0; j < 6; ++j) {
            const auto bit = i * 6 + j;
            if (bit < numBits && ((bytes[bit >> 3] >> (bit & 7)) & 1) != 0)
                value |= 1u << j;
        }
        text.push_back(kAlphabet[value]);
    }

    return text;
}

std::optional<std::vector<std::uint8_t>> decodeBinaryText(std::string_view text,
                                                          std::string* error) {
    const auto dot = text.find('.');
    if (dot == std::string_view::npos || dot == 0) {
        refuse(error, "binary text has no byte count");
        return std::nullopt;
    }

    const auto digits = text.substr(0, dot);
    if (digits.size() > 1 && digits.front() == '0') {
        refuse(error, "binary byte count has a leading zero");
        return std::nullopt;
    }

    std::size_t count = 0;
    for (const auto c : digits) {
        if (c < '0' || c > '9') {
            refuse(error, "binary byte count is not a decimal number");
            return std::nullopt;
        }
        count = count * 10 + static_cast<std::size_t>(c - '0');
        if (count > kMaxBinaryBytes) {
            refuse(error, "binary byte count exceeds the limit");
            return std::nullopt;
        }
    }

    const auto chars = text.substr(dot + 1);
    const auto numBits = count * 8;
    if (chars.size() != (numBits + 5) / 6) {
        refuse(error, "binary char count does not match the byte count");
        return std::nullopt;
    }

    std::vector<std::uint8_t> bytes(count, 0);
    for (std::size_t i = 0; i < chars.size(); ++i) {
        const auto value = kDecodeTable[static_cast<unsigned char>(chars[i])];
        if (value < 0) {
            refuse(error, "binary text has a char outside the alphabet");
            return std::nullopt;
        }

        for (std::size_t j = 0; j < 6; ++j) {
            if (((value >> j) & 1) == 0)
                continue;

            const auto bit = i * 6 + j;
            if (bit >= numBits) {
                refuse(error, "binary text has nonzero padding bits");
                return std::nullopt;
            }
            bytes[bit >> 3] = static_cast<std::uint8_t>(bytes[bit >> 3] | (1u << (bit & 7)));
        }
    }

    return bytes;
}

}  // namespace magda::sdk
