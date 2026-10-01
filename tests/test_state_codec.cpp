#include <catch2/catch_test_macros.hpp>
#include <string>

#include "magda/sdk/state/StateCodec.hpp"

using namespace magda::sdk;

namespace {

StateDocument sampleDocument() {
    StateDocument doc;
    doc.deviceType = "magdaSampler";
    doc.root.setString("samplePath", "/tmp/kick \"x\" \xC3\xA9.wav");
    doc.root.setInt("rootNote", 60);
    doc.root.setDouble("gain", 0.8);
    doc.root.setBool("loop", true);
    doc.root.setBinary("ir", {0, 1, 2, 250, 255});

    auto& step = doc.root.addChild(StateNode("STEP"));
    step.setInt("note", 36);
    step.setDouble("velocity", 1.0);
    step.addChild(StateNode("INNER")).setString("name", "a\nb");
    doc.root.addChild(StateNode("STEP")).setInt("note", 38);
    return doc;
}

DecodeStatus statusOf(std::string_view json) {
    return decodeDocument(json).status;
}

}  // namespace

TEST_CASE("A document round-trips with order, kinds and children intact", "[state-codec]") {
    const auto original = sampleDocument();
    const auto text = encodeDocument(original);
    REQUIRE(text.has_value());

    const auto decoded = decodeDocument(*text);
    REQUIRE(decoded.ok());
    CHECK(*decoded.document == original);

    const auto& props = decoded.document->root.properties();
    CHECK(props[0].key == "samplePath");
    CHECK(props[4].key == "ir");
    CHECK(decoded.document->root.find("gain")->kind() == StateValue::Kind::Double);
    CHECK(decoded.document->root.find("rootNote")->kind() == StateValue::Kind::Int64);
    CHECK(decoded.document->root.find("velocity") == nullptr);
    CHECK(decoded.document->root.children()[0].find("velocity")->kind() == StateValue::Kind::Double);
}

TEST_CASE("The writer's text re-encodes byte for byte", "[state-codec]") {
    const auto text = *encodeDocument(sampleDocument());
    const auto again = encodeDocument(*decodeDocument(text).document);
    CHECK(*again == text);
}

TEST_CASE("The writer uses the on-disk key names and tags binary", "[state-codec]") {
    const auto text = *encodeDocument(sampleDocument());
    CHECK(text.find("\"schema\": 2") != std::string::npos);
    CHECK(text.find("\"device\": \"magdaSampler\"") != std::string::npos);
    CHECK(text.find("\"props\"") != std::string::npos);
    CHECK(text.find("\"children\"") != std::string::npos);
    CHECK(text.find("\"type\": \"STEP\"") != std::string::npos);
    CHECK(text.find("{\"$bin\": \"5.") != std::string::npos);
}

TEST_CASE("Empty props and children are omitted", "[state-codec]") {
    StateDocument doc;
    doc.deviceType = "x";
    CHECK(*encodeDocument(doc) == "{\n  \"schema\": 2,\n  \"device\": \"x\"\n}\n");
    CHECK(decodeDocument(R"({"schema":2,"device":"x","props":{},"children":[]})").ok());
}

TEST_CASE("Numbers decode as int64 or double by their spelling", "[state-codec]") {
    const auto result = decodeDocument(
        R"({"schema":2,"device":"x","props":{"a":1,"b":1.0,"c":-9223372036854775808,"d":9223372036854775808,"e":1e2,"f":-0}})");
    REQUIRE(result.ok());
    const auto& root = result.document->root;
    CHECK(root.find("a")->kind() == StateValue::Kind::Int64);
    CHECK(root.find("b")->kind() == StateValue::Kind::Double);
    CHECK(*root.find("c")->int64() == std::numeric_limits<std::int64_t>::min());
    CHECK(root.find("d")->kind() == StateValue::Kind::Double);
    CHECK(root.find("e")->kind() == StateValue::Kind::Double);
    CHECK(root.find("f")->kind() == StateValue::Kind::Int64);
}

TEST_CASE("The canonical decoder rejects arrays and other object forms", "[state-codec]") {
    CHECK(statusOf(R"({"schema":2,"device":"x","props":{"a":[1,2]}})") == DecodeStatus::Invalid);
    CHECK(statusOf(R"({"schema":2,"device":"x","props":{"a":{"x":1}}})") == DecodeStatus::Invalid);
    CHECK(statusOf(R"({"schema":2,"device":"x","props":{"a":{"$bin":"1...","y":1}}})") ==
          DecodeStatus::Invalid);
    CHECK(statusOf(R"({"schema":2,"device":"x","props":{"a":{"$bin":5}}})") == DecodeStatus::Invalid);
    CHECK(statusOf(R"({"schema":2,"device":"x","props":{"a":{"$bin":""}}})") == DecodeStatus::Invalid);
    CHECK(statusOf(R"({"schema":2,"device":"x","props":{"a":null}})") == DecodeStatus::Invalid);
    CHECK(statusOf(R"({"schema":2,"device":"x","props":{"a":{"$bin":"2.AAA"}}})") == DecodeStatus::Ok);
}

TEST_CASE("The canonical decoder rejects what it would not write", "[state-codec]") {
    // Duplicate keys, empty names, unknown members, wrong shapes.
    CHECK(statusOf(R"({"schema":2,"device":"x","props":{"a":1,"a":2}})") == DecodeStatus::NotJson);
    CHECK(statusOf(R"({"schema":2,"device":"x","props":{"":1}})") == DecodeStatus::Invalid);
    CHECK(statusOf(R"({"schema":2,"device":"x","extra":1})") == DecodeStatus::Invalid);
    CHECK(statusOf(R"({"schema":2,"device":"x","params":[]})") == DecodeStatus::Invalid);
    CHECK(statusOf(R"({"schema":2,"device":"x","type":"ROOT"})") == DecodeStatus::Invalid);
    CHECK(statusOf(R"({"schema":2,"device":"x","props":[]})") == DecodeStatus::Invalid);
    CHECK(statusOf(R"({"schema":2,"device":"x","children":{}})") == DecodeStatus::Invalid);
    CHECK(statusOf(R"({"schema":2,"device":"x","children":[{"props":{}}]})") ==
          DecodeStatus::Invalid);
    CHECK(statusOf(R"({"schema":2,"device":"x","children":[{"type":"","props":{}}]})") ==
          DecodeStatus::Invalid);
    CHECK(statusOf(R"({"schema":2,"device":"x","children":[{"type":"A","bogus":1}]})") ==
          DecodeStatus::Invalid);
    CHECK(statusOf(R"({"schema":2,"device":""})") == DecodeStatus::Invalid);
    CHECK(statusOf(R"({"schema":2.0,"device":"x"})") == DecodeStatus::Invalid);
    CHECK(statusOf(R"({"schema":"2","device":"x"})") == DecodeStatus::Invalid);
}

TEST_CASE("Malformed or non-document text is told apart", "[state-codec]") {
    CHECK(statusOf("") == DecodeStatus::NotJson);
    CHECK(statusOf("not json") == DecodeStatus::NotJson);
    CHECK(statusOf(R"({"schema":2,"device":"x"} trailing)") == DecodeStatus::NotJson);
    CHECK(statusOf(R"({"schema":2,"device":"x",})") == DecodeStatus::NotJson);
    CHECK(statusOf("<PLUGIN type=\"x\"/>") == DecodeStatus::NotJson);
    CHECK(statusOf("[]") == DecodeStatus::NotADocument);
    CHECK(statusOf(R"({"schema":2})") == DecodeStatus::NotADocument);
    CHECK(statusOf(R"({"device":"x"})") == DecodeStatus::NotADocument);
    CHECK(statusOf(R"({"schema":1,"device":"x"})") == DecodeStatus::UnsupportedSchema);
}

TEST_CASE("A future schema is refused and reported as future", "[state-codec]") {
    const std::string future =
        R"({"schema":3,"device":"x","props":{"anything":[1,2,{"a":null}]},"newThing":true})";
    const auto result = decodeDocument(future);
    CHECK(result.status == DecodeStatus::FutureSchema);
    CHECK_FALSE(result.document.has_value());

    CHECK(isFutureSchema(future));
    CHECK(schemaVersionOf(future) == 3);

    CHECK_FALSE(isFutureSchema(*encodeDocument(sampleDocument())));
    CHECK_FALSE(isFutureSchema(R"({"schema":1,"device":"x"})"));
    CHECK_FALSE(isFutureSchema(R"({"schema":3})"));
    CHECK_FALSE(isFutureSchema("not json"));
    CHECK_FALSE(isFutureSchema(""));
    CHECK_FALSE(isFutureSchema("[3]"));
}

TEST_CASE("The writer refuses a document it could not read back", "[state-codec]") {
    std::string why;

    auto doc = sampleDocument();
    doc.schema = 3;
    CHECK_FALSE(encodeDocument(doc, &why).has_value());

    doc = sampleDocument();
    doc.deviceType.clear();
    CHECK_FALSE(encodeDocument(doc, &why).has_value());

    doc = sampleDocument();
    doc.root.setType("PLUGIN");
    CHECK_FALSE(encodeDocument(doc, &why).has_value());

    doc = sampleDocument();
    doc.root.addChild(StateNode());
    CHECK_FALSE(encodeDocument(doc, &why).has_value());

    doc = sampleDocument();
    doc.root.setString("bad", "\xC3");
    CHECK_FALSE(encodeDocument(doc, &why).has_value());

    doc = sampleDocument();
    StateNode* node = &doc.root;
    for (int depth = 0; depth < kMaxStateDepth + 1; ++depth)
        node = &node->addChild(StateNode("N"));
    CHECK_FALSE(encodeDocument(doc, &why).has_value());
    CHECK(why.size() > 0);
}

TEST_CASE("Strings with escapes and unicode survive", "[state-codec]") {
    const auto result = decodeDocument(
        R"({"schema":2,"device":"x","props":{"a":"é😀\n\\\"\/"}})");
    REQUIRE(result.ok());
    CHECK(result.document->root.getString("a") == "\xC3\xA9\xF0\x9F\x98\x80\n\\\"/");

    CHECK(statusOf(R"({"schema":2,"device":"x","props":{"a":"\ud83d"}})") == DecodeStatus::NotJson);
    CHECK(statusOf("{\"schema\":2,\"device\":\"x\",\"props\":{\"a\":\"\xC3\"}}") ==
          DecodeStatus::NotJson);
    CHECK(statusOf("{\"schema\":2,\"device\":\"x\",\"props\":{\"a\":\"\x01\"}}") ==
          DecodeStatus::NotJson);
}

TEST_CASE("Out-of-range numbers are refused", "[state-codec]") {
    CHECK(statusOf(R"({"schema":2,"device":"x","props":{"a":1e999}})") == DecodeStatus::NotJson);
    CHECK(statusOf(R"({"schema":2,"device":"x","props":{"a":01}})") == DecodeStatus::NotJson);
    CHECK(statusOf(R"({"schema":2,"device":"x","props":{"a":NaN}})") == DecodeStatus::NotJson);
}

TEST_CASE("Doubles round-trip exactly", "[state-codec]") {
    StateDocument doc;
    doc.deviceType = "x";
    const double values[] = {0.1, 1.0 / 3.0, 1e-300, 1.7976931348623157e308, 5e-324, -0.0, 100.0};
    for (std::size_t i = 0; i < std::size(values); ++i)
        doc.root.setDouble("d" + std::to_string(i), values[i]);

    const auto decoded = decodeDocument(*encodeDocument(doc));
    REQUIRE(decoded.ok());
    for (std::size_t i = 0; i < std::size(values); ++i)
        CHECK(*decoded.document->root.find("d" + std::to_string(i))->real() == values[i]);
}

TEST_CASE("Deep nesting is refused on read", "[state-codec]") {
    std::string json = R"({"schema":2,"device":"x")";
    std::string closing = "}";
    for (int i = 0; i < kMaxStateDepth + 1; ++i) {
        json += R"(,"children":[{"type":"N")";
        closing += "]}";
    }
    CHECK_FALSE(decodeDocument(json + closing).ok());
}
