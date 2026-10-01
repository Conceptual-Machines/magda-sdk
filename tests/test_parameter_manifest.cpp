#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>

#include "magda/sdk/device/ParameterManifest.hpp"
#include "magda/sdk/state/detail/NumberText.hpp"

using namespace magda::sdk;

namespace {

ParameterDescriptor parameter(std::string id, int index,
                              ParameterScale scale = ParameterScale::Linear) {
    ParameterDescriptor p;
    p.stableId = std::move(id);
    p.index = index;
    p.name = "Name " + std::to_string(index);
    p.scale = scale;
    return p;
}

DeviceManifest sample() {
    DeviceManifest manifest;
    manifest.deviceType = "magda_sample";
    manifest.deviceVersion = 3;

    auto cutoff = parameter("cutoff", 0, ParameterScale::Logarithmic);
    cutoff.unit = "Hz";
    cutoff.group = "Filter";
    cutoff.tooltip = "Where the filter opens \"wide\"";
    cutoff.minValue = 20.0f;
    cutoff.maxValue = 20000.0f;
    cutoff.defaultValue = 1000.0f;
    cutoff.scaleAnchor = 1000.0f;
    cutoff.widthCells = 2;
    cutoff.labelTicks = {{20.0f, "20"}, {20000.0f, "20k"}};

    auto level = parameter("level", 1, ParameterScale::FaderDB);
    level.minValue = -60.0f;
    level.maxValue = 6.0f;
    level.defaultValue = 0.0f;
    level.displayFormat = DisplayFormat::Decibels;
    level.unityPosition = 0.75f;

    auto curve = parameter("curve", 2, ParameterScale::Exponential);
    curve.exponent = 0.25f;
    curve.step = 0.5f;
    curve.bipolarModulation = true;
    curve.valueConvention = ParameterValueConvention::Normalized;

    auto mode = parameter("mode", 3, ParameterScale::Discrete);
    mode.choices = choicesFromLabels({"Poly", "Mono", "Legato"});
    mode.choices[2].value = 7.5f;
    mode.radioChoices = true;
    mode.modulatable = false;
    mode.maxValue = 2.0f;

    auto hold = parameter("hold", 4, ParameterScale::Boolean);
    hold.momentary = true;
    hold.hidden = true;
    hold.gateSlotIndex = 3;
    hold.gateNegated = true;
    hold.automatable = false;
    hold.readOnly = true;
    hold.wrapperRole = WrapperRole::WetGain;

    manifest.parameters = {cutoff, level, curve, mode, hold};
    return manifest;
}

std::string written(const DeviceManifest& manifest) {
    std::string error;
    const auto text = writeManifest(manifest, error);
    INFO(error);
    REQUIRE(text.has_value());
    return *text;
}

std::string rejection(std::string_view json) {
    std::string error;
    CHECK_FALSE(readManifest(json, error).has_value());
    CHECK_FALSE(error.empty());
    return error;
}

void replaceFirst(std::string& text, std::string_view from, std::string_view to) {
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
}

}  // namespace

TEST_CASE("A manifest round-trips", "[manifest]") {
    const auto manifest = sample();
    const auto text = written(manifest);

    std::string error;
    const auto read = readManifest(text, error);
    INFO(error);
    REQUIRE(read.has_value());
    CHECK(*read == manifest);
    CHECK(written(*read) == text);
}

TEST_CASE("The writer is deterministic and starts with the header", "[manifest]") {
    const auto text = written(sample());
    CHECK(text.starts_with(
        R"({"format":"magda.device-manifest","version":1,"deviceType":"magda_sample","deviceVersion":3,"parameterSource":"static","parameters":[{"id":"cutoff","index":0,)"));
}

TEST_CASE("Fields a scale does not use are not written and read as defaults", "[manifest]") {
    auto manifest = sample();
    auto& p = manifest.parameters[4];  // boolean
    p.scaleAnchor = 5.0f;
    p.exponent = 9.0f;
    p.choices = choicesFromLabels({"Off", "On"});

    const auto text = written(manifest);
    CHECK(text.find("anchor\":5") == std::string::npos);
    CHECK(text.find("exponent\":9") == std::string::npos);

    std::string error;
    const auto read = readManifest(text, error);
    REQUIRE(read.has_value());
    CHECK(read->parameters[4].scaleAnchor == 0.0f);
    CHECK(read->parameters[4].exponent == 1.0f);
    CHECK(read->parameters[4].choices.empty());
}

TEST_CASE("An anchor outside the range is not written", "[manifest]") {
    auto manifest = sample();
    manifest.parameters[0].scaleAnchor = 99999.0f;
    CHECK(written(manifest).find("anchor") == std::string::npos);
}

TEST_CASE("Both parameter sources round-trip", "[manifest]") {
    auto manifest = sample();
    manifest.parameterSource = ParameterSource::State;
    const auto text = written(manifest);
    CHECK(text.find("\"parameterSource\":\"state\"") != std::string::npos);

    std::string error;
    const auto read = readManifest(text, error);
    REQUIRE(read.has_value());
    CHECK(read->parameterSource == ParameterSource::State);
}

TEST_CASE("The writer refuses what the format forbids", "[manifest]") {
    std::string error;

    auto repeatedId = sample();
    repeatedId.parameters[1].stableId = "cutoff";
    CHECK_FALSE(writeManifest(repeatedId, error).has_value());

    auto repeatedIndex = sample();
    repeatedIndex.parameters[1].index = 0;
    CHECK_FALSE(writeManifest(repeatedIndex, error).has_value());

    auto unresolved = sample();
    unresolved.parameters[1].stableId.clear();
    CHECK_FALSE(writeManifest(unresolved, error).has_value());

    auto unindexed = sample();
    unindexed.parameters[1].index = -1;
    CHECK_FALSE(writeManifest(unindexed, error).has_value());

    auto nonFinite = sample();
    nonFinite.parameters[1].maxValue = std::numeric_limits<float>::infinity();
    CHECK_FALSE(writeManifest(nonFinite, error).has_value());

    auto inverted = sample();
    inverted.parameters[1].minValue = 100.0f;
    CHECK_FALSE(writeManifest(inverted, error).has_value());

    auto repeatedChoice = sample();
    repeatedChoice.parameters[3].choices[1].id = "Poly";
    CHECK_FALSE(writeManifest(repeatedChoice, error).has_value());

    auto badText = sample();
    badText.parameters[1].name = "\xff";
    CHECK_FALSE(writeManifest(badText, error).has_value());

    auto noType = sample();
    noType.deviceType.clear();
    CHECK_FALSE(writeManifest(noType, error).has_value());

    auto version0 = sample();
    version0.deviceVersion = 0;
    CHECK_FALSE(writeManifest(version0, error).has_value());
}

TEST_CASE("The reader refuses what the format forbids", "[manifest]") {
    const auto good = written(sample());

    SECTION("an unknown top-level key") {
        auto text = good;
        replaceFirst(text, "\"parameters\"", "\"extra\":1,\"parameters\"");
        rejection(text);
    }
    SECTION("an unknown parameter key") {
        auto text = good;
        replaceFirst(text, "\"name\":\"Name 0\"", "\"name\":\"Name 0\",\"colour\":1");
        rejection(text);
    }
    SECTION("a key another scale owns") {
        auto text = good;
        replaceFirst(text, "\"kind\":\"faderDb\"", "\"kind\":\"faderDb\",\"anchor\":1.0");
        rejection(text);
    }
    SECTION("another format") {
        auto text = good;
        replaceFirst(text, "magda.device-manifest", "other");
        rejection(text);
    }
    SECTION("another version") {
        auto text = good;
        replaceFirst(text, "\"version\":1", "\"version\":2");
        rejection(text);
    }
    SECTION("a version that is not an integer") {
        auto text = good;
        replaceFirst(text, "\"version\":1", "\"version\":1.0");
        rejection(text);
    }
    SECTION("a repeated id") {
        auto text = good;
        replaceFirst(text, "\"id\":\"level\"", "\"id\":\"cutoff\"");
        rejection(text);
    }
    SECTION("a repeated index") {
        auto text = good;
        replaceFirst(text, "\"index\":1", "\"index\":0");
        rejection(text);
    }
    SECTION("an unknown scale kind") {
        auto text = good;
        replaceFirst(text, "\"kind\":\"logarithmic\"", "\"kind\":\"cubic\"");
        rejection(text);
    }
    SECTION("an anchor outside the range") {
        auto text = good;
        replaceFirst(text, "\"anchor\":1000.0", "\"anchor\":5.0");
        rejection(text);
    }
    SECTION("a missing required field") {
        auto text = good;
        replaceFirst(text, "\"min\":20.0,", "");
        rejection(text);
    }
    SECTION("a discrete scale without choices") {
        auto text = good;
        const auto at = text.find(",\"choices\":[");
        REQUIRE(at != std::string::npos);
        text.erase(at, text.find(']', at) + 1 - at);
        rejection(text);
    }
    SECTION("a repeated choice id") {
        auto text = good;
        replaceFirst(text, "\"id\":\"Mono\"", "\"id\":\"Poly\"");
        rejection(text);
    }
    SECTION("a duplicate key") {
        auto text = good;
        replaceFirst(text, "\"deviceVersion\":3", "\"deviceVersion\":3,\"deviceVersion\":3");
        rejection(text);
    }
    SECTION("trailing content") {
        rejection(good + " x");
    }
    SECTION("not an object") {
        rejection("[]");
    }
}

TEST_CASE("Hand-written integers read as floats", "[manifest]") {
    std::string error;
    const auto read = readManifest(
        R"({"format":"magda.device-manifest","version":1,"deviceType":"t","deviceVersion":1,
            "parameterSource":"static","parameters":[
            {"id":"a","index":0,"name":"A","min":0,"max":10,"default":5,"scale":{"kind":"linear"}}]})",
        error);
    INFO(error);
    REQUIRE(read.has_value());
    CHECK(read->parameters[0].maxValue == 10.0f);
}

TEST_CASE("Floats survive the text they are written as", "[manifest]") {
    std::mt19937 random(2939);
    for (int i = 0; i < 200000; ++i) {
        std::uint32_t bits = random();
        float value;
        std::memcpy(&value, &bits, sizeof value);
        if (!std::isfinite(value))
            continue;
        const auto text = detail::writeFloat(value);
        REQUIRE(static_cast<float>(detail::parseNumberToken(text)) == value);
    }
    CHECK(detail::writeFloat(0.8f) == "0.8");
    CHECK(detail::writeFloat(1000.0f) == "1000.0");
}

namespace {

class FakeDevice : public Device {
  public:
    DeviceProperties properties() const override {
        DeviceProperties properties;
        properties.pluginId = "fake";
        properties.deviceVersion = 2;
        properties.parameterSource = ParameterSource::State;
        return properties;
    }
    void process(ProcessContext&) override {}
    int parameterCount() const override {
        return 3;
    }
    ParameterDescriptor parameterDescriptor(int slot) const override {
        ParameterDescriptor p;
        p.name = "P" + std::to_string(slot);
        if (slot == 1)
            p.stableId = "named";
        if (slot == 2)
            p.index = 10;
        return p;
    }
};

}  // namespace

TEST_CASE("buildManifest resolves ids and indices from the device", "[manifest]") {
    FakeDevice device;
    const auto manifest = buildManifest(device);
    CHECK(manifest.deviceType == "fake");
    CHECK(manifest.deviceVersion == 2);
    CHECK(manifest.parameterSource == ParameterSource::State);
    REQUIRE(manifest.parameters.size() == 3);
    CHECK(manifest.parameters[0].stableId == "fake_param_0");
    CHECK(manifest.parameters[1].stableId == "named");
    CHECK(manifest.parameters[1].index == 1);
    CHECK(manifest.parameters[2].stableId == "fake_param_10");
    CHECK(manifest.parameters[2].index == 10);

    std::string error;
    CHECK(writeManifest(manifest, error).has_value());
}

TEST_CASE("A device with no parameters has an empty manifest", "[manifest]") {
    class Empty : public Device {
      public:
        DeviceProperties properties() const override {
            DeviceProperties properties;
            properties.pluginId = "empty";
            return properties;
        }
        void process(ProcessContext&) override {}
    } device;
    const auto manifest = buildManifest(device);
    CHECK(manifest.parameters.empty());
    CHECK(manifest.deviceVersion == 1);
    CHECK(manifest.parameterSource == ParameterSource::Static);

    std::string error;
    const auto text = writeManifest(manifest, error);
    REQUIRE(text.has_value());
    CHECK(text->ends_with("\"parameters\":[]}"));
    CHECK(readManifest(*text, error).has_value());
}
