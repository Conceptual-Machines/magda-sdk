#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

#include "magda/sdk/state/StateNode.hpp"

using magda::sdk::StateNode;
using magda::sdk::StateValue;

namespace {

StateNode withString(const char* text) {
    StateNode node;
    node.setString("k", text);
    return node;
}

}  // namespace

TEST_CASE("Properties keep insertion order and unique keys", "[state-node]") {
    StateNode node("PAD");
    node.setInt("b", 1);
    node.setString("a", "x");
    node.setBool("c", true);
    node.setInt("b", 2);

    const auto props = node.properties();
    REQUIRE(props.size() == 3);
    CHECK(props[0].key == "b");
    CHECK(props[1].key == "a");
    CHECK(props[2].key == "c");
    CHECK(node.getInt("b") == 2);
}

TEST_CASE("Remove drops a property and keeps the order of the rest", "[state-node]") {
    StateNode node;
    node.setInt("a", 1);
    node.setInt("b", 2);
    node.setInt("c", 3);

    CHECK(node.remove("b"));
    CHECK_FALSE(node.remove("b"));
    REQUIRE(node.properties().size() == 2);
    CHECK(node.properties()[0].key == "a");
    CHECK(node.properties()[1].key == "c");
}

TEST_CASE("An empty key and a non-finite double are refused", "[state-node]") {
    StateNode node;
    CHECK_FALSE(node.setInt("", 1));
    CHECK_FALSE(node.setDouble("d", std::numeric_limits<double>::infinity()));
    CHECK_FALSE(node.setDouble("d", std::numeric_limits<double>::quiet_NaN()));
    CHECK_FALSE(node.has("d"));
    CHECK(node.setDouble("d", 0.5));
    CHECK(node.has("d"));
}

TEST_CASE("A missing key reads as the fallback, a mismatched kind never does", "[state-node]") {
    StateNode node;
    node.setString("name", "abc");

    CHECK(node.getInt("missing", 7) == 7);
    CHECK(node.getDouble("missing", 1.5) == 1.5);
    CHECK(node.getBool("missing", true));
    CHECK(node.getString("missing", "dflt") == "dflt");

    CHECK(node.getInt("name", 7) == 0);
    CHECK(node.getBool("name", true) == false);
}

TEST_CASE("Numbers and bools coerce across kinds like juce::var", "[state-node]") {
    StateNode node;
    node.setInt("i", 60);
    node.setDouble("d", 2.9);
    node.setDouble("neg", -2.9);
    node.setBool("t", true);

    CHECK(node.getDouble("i") == 60.0);
    CHECK(node.getInt("d") == 2);
    CHECK(node.getInt("neg") == -2);
    CHECK(node.getInt64("d") == 2);
    CHECK(node.getBool("d"));
    CHECK(node.getInt("t") == 1);
    CHECK(node.getDouble("t") == 1.0);
    CHECK(node.getString("t") == "1");
    CHECK(node.getString("i") == "60");

    node.setInt("big", std::int64_t{1} << 33);
    CHECK(node.getInt("big") == std::numeric_limits<int>::max());
    node.setInt("small", -(std::int64_t{1} << 33));
    CHECK(node.getInt("small") == std::numeric_limits<int>::min());
    CHECK(withString("99999999999").getInt("k") == std::numeric_limits<int>::max());
    CHECK(withString("-99999999999").getInt("k") == std::numeric_limits<int>::min());
    CHECK(node.getInt64("big") == (std::int64_t{1} << 33));

    node.setDouble("zero", 0.0);
    CHECK_FALSE(node.getBool("zero"));
}

TEST_CASE("Strings coerce to numbers and bools like juce::String", "[state-node]") {
    CHECK(withString("60").getInt("k") == 60);
    CHECK(withString("  -12abc").getInt("k") == -12);
    CHECK(withString("1.5").getInt("k") == 1);
    CHECK(withString("+5").getInt("k") == 5);
    CHECK(withString("99999999999999999999").getInt64("k") ==
          std::numeric_limits<std::int64_t>::max());
    CHECK(withString("-99999999999999999999").getInt64("k") ==
          std::numeric_limits<std::int64_t>::min());
    CHECK(withString("9223372036854775807").getInt64("k") ==
          std::numeric_limits<std::int64_t>::max());
    CHECK(withString("-9223372036854775808").getInt64("k") ==
          std::numeric_limits<std::int64_t>::min());
    CHECK(withString("4294967297").getInt("k") == std::numeric_limits<int>::max());
    CHECK(withString("abc").getInt("k") == 0);
    CHECK(withString("").getInt("k") == 0);

    CHECK(withString("1.5").getDouble("k") == 1.5);
    CHECK(withString("  -0.25x").getDouble("k") == -0.25);
    CHECK(withString("1e3").getDouble("k") == 1000.0);
    CHECK(withString("+2").getDouble("k") == 2.0);
    CHECK(withString("abc").getDouble("k") == 0.0);
    CHECK(withString(".5").getDouble("k") == 0.5);

    CHECK(withString("1").getBool("k"));
    CHECK(withString("true").getBool("k"));
    CHECK(withString("  TRUE ").getBool("k"));
    CHECK(withString("Yes").getBool("k"));
    CHECK_FALSE(withString("0").getBool("k"));
    CHECK_FALSE(withString("false").getBool("k"));
    CHECK_FALSE(withString("").getBool("k"));
    CHECK(withString("2.5").getBool("k"));
    CHECK_FALSE(withString("0.5").getBool("k"));
}

TEST_CASE("A string that coerces to a non-finite double reads as the fallback", "[state-node]") {
    CHECK(withString("nan").getDouble("k", 3.0) == 3.0);
    CHECK(withString("inf").getDouble("k", 3.0) == 3.0);
    CHECK(withString("-Infinity").getDouble("k", 3.0) == 3.0);
    CHECK(withString("1e999").getDouble("k", 3.0) == 3.0);
    CHECK_FALSE(withString("nan").find("k")->toDouble().has_value());
}

TEST_CASE("A double reads back as a string that parses to the same double", "[state-node]") {
    for (const double value : {0.1, 1.0, -2.5, 1.0 / 3.0, 1e-7, 123456789012.5}) {
        StateNode node;
        node.setDouble("d", value);
        node.setString("s", node.getString("d"));
        CHECK(node.getDouble("s") == value);
    }
}

TEST_CASE("Binary is never coerced and reads as zero from a scalar getter", "[state-node]") {
    StateNode node;
    node.setBinary("b", {1, 2, 3});
    node.setInt("i", 5);

    REQUIRE(node.getBinary("b") != nullptr);
    CHECK(node.getBinary("b")->size() == 3);
    CHECK(node.getBinary("i") == nullptr);
    CHECK(node.getInt("b", 9) == 0);
    CHECK(node.getDouble("b", 9.0) == 0.0);
    CHECK_FALSE(node.getBool("b", true));
    CHECK(node.getString("b") == "3.AHv.");
}

TEST_CASE("Children keep order and are found by type", "[state-node]") {
    StateNode node;
    node.addChild(StateNode("A")).setInt("n", 1);
    node.addChild(StateNode("B"));
    node.addChild(StateNode("A")).setInt("n", 2);

    REQUIRE(node.children().size() == 3);
    CHECK(node.findChild("A")->getInt("n") == 1);
    CHECK(node.findChild("C") == nullptr);
    CHECK(node.removeChild(0));
    CHECK(node.children().front().type() == "B");
    CHECK_FALSE(node.removeChild(5));
}
