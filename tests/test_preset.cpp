#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <string>

#include "magda/sdk/preset/Preset.hpp"

using namespace magda::sdk;

namespace {

Preset magdaPreset() {
    Preset preset;
    preset.id = "3f2b8c1e-9a4d-4e6f-8b7a-1c2d3e4f5a6b";
    preset.writer = "MAGDA 1.0.0";
    preset.deviceType = "magdaSampler";
    preset.deviceVersion = 2;
    preset.name = "Kick \"Deep\" \xC3\xA9";
    preset.author = "Ada";
    preset.tags = {"drums", "one-shot"};
    preset.created = "2026-10-01T12:30:45Z";
    preset.parameters = {{"magdaSampler_param_0", 0.25}, {"gain", 1.0}, {"cutoff", -3.5e-7}};

    StateDocument state;
    state.deviceType = "magdaSampler";
    state.root.setString("source", "kick");
    state.root.setInt("rootNote", 60);
    state.root.setBinary("ir", {0, 1, 250});
    state.root.addChild(StateNode("STEP")).setDouble("velocity", 0.5);
    preset.device = PresetMagdaDevice{state};

    preset.assets = {{"kick", "../Samples/kick one.wav", std::string(64, 'a')},
                     {"snare", "snare.wav", std::nullopt}};
    preset.host = R"({"magda":{"version":1,"list":[1,2.5,null,true,"x"],"n":{"b":1,"a":2.0}}})";
    return preset;
}

Preset pluginPreset() {
    Preset preset;
    preset.id = "legacy-id-0123";
    preset.writer = "Other 2.1";
    preset.deviceType = "serum";
    preset.name = "Lead";
    preset.created = "2024-02-29T00:00:00Z";
    preset.valueDomain = PresetValueDomain::Normalized;
    preset.parameters = {{"serum_param_3", 0.75}};

    PresetPluginDevice plugin;
    plugin.format = "VST3";
    plugin.uniqueId = "VST3-Serum-1234";
    plugin.fileOrIdentifier = "/Library/Audio/Serum.vst3";
    plugin.vst3ClassId = "ABCDEF0123456789ABCDEF0123456789";
    plugin.chunk = {0, 1, 2, 3, 4, 255};
    plugin.vst3Preset = Binary{9, 8};
    preset.device = plugin;
    return preset;
}

std::string write(const Preset& preset) {
    std::string error;
    const auto text = writePreset(preset, error);
    INFO(error);
    REQUIRE(text.has_value());
    return *text;
}

PresetStatus statusOf(const std::string& text) {
    return readPreset(text).status;
}

std::string replace(std::string text, const std::string& from, const std::string& to) {
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

}  // namespace

TEST_CASE("A magda preset round-trips with every field intact", "[preset]") {
    const auto original = magdaPreset();
    const auto result = readPreset(write(original));
    REQUIRE(result.ok());
    CHECK(*result.preset == original);
}

TEST_CASE("A plugin preset round-trips with its chunk and class id", "[preset]") {
    const auto original = pluginPreset();
    const auto result = readPreset(write(original));
    REQUIRE(result.ok());
    CHECK(*result.preset == original);
    CHECK(std::get<PresetPluginDevice>(result.preset->device).chunk == Binary{0, 1, 2, 3, 4, 255});
}

TEST_CASE("The writer's text re-encodes byte for byte", "[preset]") {
    for (const auto& preset : {magdaPreset(), pluginPreset()}) {
        const auto text = write(preset);
        CHECK(write(*readPreset(text).preset) == text);
    }
}

TEST_CASE("The writer spells the documented shape", "[preset]") {
    const auto text = write(magdaPreset());
    CHECK(text.starts_with("{\n  \"format\": \"magda.preset\",\n  \"version\": 1,\n  \"id\": "));
    CHECK(text.find("\"valueDomain\": \"display\"") != std::string::npos);
    CHECK(text.find("\"kind\": \"magda\"") != std::string::npos);
    CHECK(text.find("\"schema\": 2") != std::string::npos);
    CHECK(text.find("\"host\": {") != std::string::npos);

    const auto plugin = write(pluginPreset());
    CHECK(plugin.find("\"chunk\": \"AAECAwT/\"") != std::string::npos);
    CHECK(plugin.find("\"vst3Preset\": \"CQg=\"") != std::string::npos);
}

TEST_CASE("Optional members are omitted when empty", "[preset]") {
    auto preset = pluginPreset();
    const auto text = write(preset);
    CHECK(text.find("\"author\"") == std::string::npos);
    CHECK(text.find("\"tags\"") == std::string::npos);
    CHECK(text.find("\"assets\"") == std::string::npos);
    CHECK(text.find("\"host\"") == std::string::npos);
}

TEST_CASE("The host object is kept: order, kinds and nesting", "[preset]") {
    const auto result = readPreset(write(magdaPreset()));
    REQUIRE(result.ok());
    CHECK(*result.preset->host ==
          R"({"magda":{"version":1,"list":[1,2.5,null,true,"x"],"n":{"b":1,"a":2.0}}})");
}

TEST_CASE("Unknown members are refused everywhere but inside host", "[preset]") {
    const auto text = write(magdaPreset());
    CHECK(statusOf(replace(text, "\"version\": 1,", "\"version\": 1,\n  \"extra\": 1,")) ==
          PresetStatus::Invalid);
    CHECK(statusOf(replace(text, "\"kind\": \"magda\",", "\"kind\": \"magda\", \"x\": 1,")) ==
          PresetStatus::Invalid);
    CHECK(statusOf(replace(text, "\"key\": \"kick\",", "\"key\": \"kick\", \"x\": 1,")) ==
          PresetStatus::Invalid);
    CHECK(statusOf(replace(text, "\"list\":", "\"unknown\": {\"a\": [1]}, \"list\":")) ==
          PresetStatus::Ok);
}

TEST_CASE("A future or older version is refused without reading further", "[preset]") {
    const auto text = write(magdaPreset());
    const auto future = replace(text, "\"version\": 1,", "\"version\": 2,\n  \"surprise\": [],");
    CHECK(statusOf(future) == PresetStatus::FutureVersion);
    CHECK(isFuturePreset(future));
    CHECK_FALSE(isFuturePreset(text));
    CHECK(statusOf(replace(text, "\"version\": 1", "\"version\": 0")) ==
          PresetStatus::UnsupportedVersion);
}

TEST_CASE("A device state from a newer schema is a future preset", "[preset]") {
    const auto text = write(magdaPreset());
    CHECK(statusOf(replace(text, "\"schema\": 2", "\"schema\": 3")) == PresetStatus::FutureVersion);
}

TEST_CASE("Text that is not a preset is told apart", "[preset]") {
    CHECK(statusOf("not json") == PresetStatus::NotJson);
    CHECK(statusOf("[]") == PresetStatus::NotAPreset);
    CHECK(statusOf("{\"kind\": \"device\", \"payload\": {}}") == PresetStatus::NotAPreset);
    CHECK(statusOf("{\"format\": \"other\", \"version\": 1}") == PresetStatus::NotAPreset);
    CHECK(statusOf("{\"format\": \"magda.preset\", \"version\": \"1\"}") == PresetStatus::Invalid);
}

TEST_CASE("The reader refuses content the format does not allow", "[preset]") {
    const auto magda = write(magdaPreset());
    const auto plugin = write(pluginPreset());

    CHECK(statusOf(replace(magda, "2026-10-01T12:30:45Z", "2026-13-01T12:30:45Z")) ==
          PresetStatus::Invalid);
    CHECK(statusOf(replace(magda, "2026-10-01T12:30:45Z", "2026-10-01 12:30:45Z")) ==
          PresetStatus::Invalid);
    CHECK(statusOf(replace(plugin, "2024-02-29", "2023-02-29")) == PresetStatus::Invalid);
    CHECK(statusOf(replace(magda, "3f2b8c1e-9a4d-4e6f-8b7a-1c2d3e4f5a6b", "has space")) ==
          PresetStatus::Invalid);
    CHECK(statusOf(replace(magda, "\"valueDomain\": \"display\"", "\"valueDomain\": \"raw\"")) ==
          PresetStatus::Invalid);
    CHECK(statusOf(replace(magda, "\"gain\": 1.0", "\"gain\": \"1\"")) == PresetStatus::Invalid);
    CHECK(statusOf(replace(magda, "\"gain\": 1.0", "\"magdaSampler_param_0\": 1.0")) ==
          PresetStatus::NotJson);
    CHECK(statusOf(replace(magda, "\"device\": \"magdaSampler\"", "\"device\": \"other\"")) ==
          PresetStatus::Invalid);
    CHECK(statusOf(replace(magda, "\"host\": {", "\"host\": [")) == PresetStatus::NotJson);
    CHECK(statusOf(replace(plugin, "\"kind\": \"plugin\"", "\"kind\": \"vst\"")) ==
          PresetStatus::Invalid);
    CHECK(statusOf(replace(plugin, "AAECAwT/", "AAECAwT_")) == PresetStatus::Invalid);
    CHECK(statusOf(replace(plugin, "AAECAwT/", "AAECAwT")) == PresetStatus::Invalid);
    CHECK(statusOf(replace(plugin, "CQg=", "CQh=")) == PresetStatus::Invalid);
    CHECK(statusOf(replace(magda, "\"snare.wav\"", "\"/snare.wav\"")) == PresetStatus::Invalid);
    CHECK(statusOf(replace(magda, std::string(64, 'a'), std::string(64, 'A'))) ==
          PresetStatus::Invalid);
    CHECK(statusOf(replace(magda, "\"key\": \"snare\"", "\"key\": \"kick\"")) ==
          PresetStatus::Invalid);
}

TEST_CASE("The writer refuses what the reader would", "[preset]") {
    std::string error;

    auto preset = magdaPreset();
    preset.created = "yesterday";
    CHECK_FALSE(writePreset(preset, error).has_value());
    CHECK_FALSE(error.empty());

    preset = magdaPreset();
    std::get<PresetMagdaDevice>(preset.device).state.deviceType = "other";
    CHECK_FALSE(writePreset(preset, error).has_value());

    preset = magdaPreset();
    preset.assets[0].path = "a\\b";
    CHECK_FALSE(writePreset(preset, error).has_value());

    preset = magdaPreset();
    preset.host = "[1]";
    CHECK_FALSE(writePreset(preset, error).has_value());

    preset = magdaPreset();
    preset.parameters.push_back({"gain", 0.0});
    CHECK_FALSE(writePreset(preset, error).has_value());

    preset = magdaPreset();
    preset.parameters[0].value = std::numeric_limits<double>::infinity();
    CHECK_FALSE(writePreset(preset, error).has_value());
}

TEST_CASE("An empty chunk round-trips", "[preset]") {
    auto preset = pluginPreset();
    std::get<PresetPluginDevice>(preset.device).chunk.clear();
    std::get<PresetPluginDevice>(preset.device).vst3Preset.reset();
    const auto result = readPreset(write(preset));
    REQUIRE(result.ok());
    CHECK(*result.preset == preset);
}

TEST_CASE("Asset paths are relative with forward slashes", "[preset]") {
    CHECK(isValidAssetPath("kick.wav"));
    CHECK(isValidAssetPath("../Samples/kick one.wav"));
    CHECK(isValidAssetPath("a/b/c.wav"));
    CHECK_FALSE(isValidAssetPath(""));
    CHECK_FALSE(isValidAssetPath("/abs.wav"));
    CHECK_FALSE(isValidAssetPath("C:/abs.wav"));
    CHECK_FALSE(isValidAssetPath("a\\b.wav"));
    CHECK_FALSE(isValidAssetPath("a//b.wav"));
    CHECK_FALSE(isValidAssetPath("./a.wav"));
    CHECK_FALSE(isValidAssetPath("a/"));
}

TEST_CASE("Resolving assets reports what the host did not find", "[preset]") {
    const auto preset = magdaPreset();
    const auto resolution = resolveAssets(preset, [](const PresetAsset& asset) {
        if (asset.key == "kick")
            return AssetLookup{AssetLookup::Status::Found, "/samples/kick one.wav"};
        return AssetLookup{AssetLookup::Status::Missing, {}};
    });

    REQUIRE(resolution.assets.size() == 2);
    CHECK_FALSE(resolution.complete());
    CHECK(resolution.unresolvedKeys() == std::vector<std::string>{"snare"});
    CHECK(resolution.find("kick")->lookup.location == "/samples/kick one.wav");
    CHECK(resolution.find("nope") == nullptr);

    CHECK(resolveAssets(preset, [](const PresetAsset&) {
              return AssetLookup{AssetLookup::Status::Found, "x"};
          }).complete());
    CHECK(resolveAssets(Preset{}, nullptr).complete());
}
