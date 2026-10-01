#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "magda/sdk/state/BinaryText.hpp"

using magda::sdk::decodeBinaryText;
using magda::sdk::encodeBinaryText;

TEST_CASE("Binary text matches the juce::MemoryBlock encoding", "[binary-text]") {
    // Vectors produced by juce::MemoryBlock::toBase64Encoding().
    CHECK(encodeBinaryText({}) == "0.");
    CHECK(encodeBinaryText(std::vector<std::uint8_t>{0}) == "1...");
    CHECK(encodeBinaryText(std::vector<std::uint8_t>{1, 2, 3}) == "3.AHv.");
}

TEST_CASE("Binary text round-trips every length and byte value", "[binary-text]") {
    for (std::size_t size = 0; size < 70; ++size) {
        std::vector<std::uint8_t> bytes(size);
        for (std::size_t i = 0; i < size; ++i)
            bytes[i] = static_cast<std::uint8_t>((i * 37 + size * 11) & 0xFF);

        const auto text = encodeBinaryText(bytes);
        const auto decoded = decodeBinaryText(text);
        REQUIRE(decoded.has_value());
        CHECK(*decoded == bytes);
    }

    std::vector<std::uint8_t> all(256);
    for (std::size_t i = 0; i < all.size(); ++i)
        all[i] = static_cast<std::uint8_t>(i);
    CHECK(*decodeBinaryText(encodeBinaryText(all)) == all);
}

TEST_CASE("Binary text refuses what the encoder would never write", "[binary-text]") {
    std::string why;
    CHECK_FALSE(decodeBinaryText("", &why));
    CHECK_FALSE(decodeBinaryText("3", &why));
    CHECK_FALSE(decodeBinaryText(".AHv.", &why));
    CHECK_FALSE(decodeBinaryText("03.AHv.", &why));
    CHECK_FALSE(decodeBinaryText("-1.AHv.", &why));
    CHECK_FALSE(decodeBinaryText("3x.AHv.", &why));
    CHECK_FALSE(decodeBinaryText("3.AH", &why));
    CHECK_FALSE(decodeBinaryText("3.AHv..", &why));
    CHECK_FALSE(decodeBinaryText("3.AHv!", &why));
    CHECK_FALSE(decodeBinaryText("3.AHv=", &why));
    CHECK_FALSE(decodeBinaryText("3.AHv ", &why));
    CHECK_FALSE(decodeBinaryText("99999999999999999999.A", &why));
    CHECK_FALSE(decodeBinaryText("268435457.A", &why));
    CHECK(why.size() > 0);
}

TEST_CASE("Binary text refuses nonzero padding bits", "[binary-text]") {
    // One byte is 8 bits in 2 chars (12 bits): the last 4 bits are padding and must be zero.
    CHECK(decodeBinaryText("1..B").has_value());
    CHECK_FALSE(decodeBinaryText("1..Q").has_value());
}
