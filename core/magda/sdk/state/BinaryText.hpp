#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace magda::sdk {

/// Largest binary payload the decoder accepts, in bytes.
inline constexpr std::size_t kMaxBinaryBytes = std::size_t{256} << 20;

/**
 * @brief Encode @p bytes as the text a `$bin` value carries.
 *
 * `<byte count>.<chars>`: six bits per char, least significant bit first, over the alphabet
 * `.A-Za-z0-9+`. Byte-identical to juce::MemoryBlock::toBase64Encoding.
 */
std::string encodeBinaryText(std::span<const std::uint8_t> bytes);

/**
 * @brief Decode the text of a `$bin` value; strict.
 *
 * Refuses a malformed or non-canonical count, a char outside the alphabet, a char count other
 * than ceil(8 * count / 6), nonzero padding bits, and a count above kMaxBinaryBytes. On refusal
 * returns nullopt and, when @p error is set, says why.
 */
std::optional<std::vector<std::uint8_t>> decodeBinaryText(std::string_view text,
                                                          std::string* error = nullptr);

}  // namespace magda::sdk
