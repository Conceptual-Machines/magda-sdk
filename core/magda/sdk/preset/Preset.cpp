#include "magda/sdk/preset/Preset.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <span>

#include "magda/sdk/state/BinaryText.hpp"
#include "magda/sdk/state/detail/Json.hpp"
#include "magda/sdk/state/detail/NumberText.hpp"
#include "magda/sdk/state/detail/StateJson.hpp"

namespace magda::sdk {

namespace {

using detail::JsonValue;

constexpr int kMaxPresetJsonDepth = kMaxStateDepth * 2 + 8 + 6;
constexpr std::size_t kMaxIdLength = 128;

constexpr std::string_view kBase64Alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encodeBase64(std::span<const std::uint8_t> bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::size_t left = bytes.size() - i;
        const unsigned b0 = bytes[i];
        const unsigned b1 = left > 1 ? bytes[i + 1] : 0u;
        const unsigned b2 = left > 2 ? bytes[i + 2] : 0u;
        out.push_back(kBase64Alphabet[b0 >> 2]);
        out.push_back(kBase64Alphabet[((b0 & 0x3u) << 4) | (b1 >> 4)]);
        out.push_back(left > 1 ? kBase64Alphabet[((b1 & 0xFu) << 2) | (b2 >> 6)] : '=');
        out.push_back(left > 2 ? kBase64Alphabet[b2 & 0x3Fu] : '=');
    }
    return out;
}

/// Standard base64 with padding; refuses whitespace, other alphabets and nonzero padding bits.
std::optional<Binary> decodeBase64(std::string_view text) {
    if (text.size() % 4 != 0)
        return std::nullopt;

    std::array<std::int8_t, 256> table{};
    for (auto& entry : table)
        entry = -1;
    for (std::size_t i = 0; i < kBase64Alphabet.size(); ++i)
        table[static_cast<unsigned char>(kBase64Alphabet[i])] = static_cast<std::int8_t>(i);

    Binary out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const bool last = i + 4 == text.size();
        const std::size_t pad = last ? (text[i + 2] == '=' ? 2 : (text[i + 3] == '=' ? 1 : 0)) : 0;
        if (pad == 2 && text[i + 3] != '=')
            return std::nullopt;

        unsigned group = 0;
        for (std::size_t k = 0; k < 4 - pad; ++k) {
            const auto value = table[static_cast<unsigned char>(text[i + k])];
            if (value < 0)
                return std::nullopt;
            group = (group << 6) | static_cast<unsigned>(value);
        }
        group <<= 6 * pad;

        if (pad == 2 && (group & 0xFFFFu) != 0)
            return std::nullopt;
        if (pad == 1 && (group & 0xFFu) != 0)
            return std::nullopt;

        out.push_back(static_cast<std::uint8_t>(group >> 16));
        if (pad < 2)
            out.push_back(static_cast<std::uint8_t>((group >> 8) & 0xFFu));
        if (pad < 1)
            out.push_back(static_cast<std::uint8_t>(group & 0xFFu));
    }
    if (out.size() > kMaxBinaryBytes)
        return std::nullopt;
    return out;
}

bool isLowerHex(std::string_view text, std::size_t length) {
    return text.size() == length && std::ranges::all_of(text, [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

bool isValidCreated(std::string_view text) {
    if (text.size() != 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
        text[13] != ':' || text[16] != ':' || text[19] != 'Z')
        return false;

    const auto number = [&](std::size_t at, std::size_t length) {
        int value = 0;
        for (std::size_t i = at; i < at + length; ++i) {
            if (text[i] < '0' || text[i] > '9')
                return -1;
            value = value * 10 + (text[i] - '0');
        }
        return value;
    };

    const int year = number(0, 4);
    const int month = number(5, 2);
    const int day = number(8, 2);
    const int hour = number(11, 2);
    const int minute = number(14, 2);
    const int second = number(17, 2);
    if (year < 0 || month < 1 || month > 12 || day < 1 || hour < 0 || hour > 23 || minute < 0 ||
        minute > 59 || second < 0 || second > 59)
        return false;

    constexpr std::array<int, 12> days = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return day <= days[static_cast<std::size_t>(month - 1)] + (month == 2 && leap ? 1 : 0);
}

bool isValidId(std::string_view id) {
    return !id.empty() && id.size() <= kMaxIdLength &&
           std::ranges::all_of(id, [](char c) { return c > 0x20 && c < 0x7F; });
}

/// The first rule @p preset breaks beyond JSON shape, shared by the reader and the writer.
std::optional<std::string> checkPreset(const Preset& preset) {
    if (!isValidId(preset.id))
        return "\"id\" must be 1 to 128 printable ASCII characters without spaces";
    if (preset.deviceType.empty())
        return "\"deviceType\" must not be empty";
    if (preset.deviceVersion < 1)
        return "\"deviceVersion\" must be at least 1";
    if (preset.name.empty())
        return "\"name\" must not be empty";
    for (const auto& tag : preset.tags)
        if (tag.empty())
            return "a tag must not be empty";
    if (!isValidCreated(preset.created))
        return "\"created\" must be YYYY-MM-DDTHH:MM:SSZ";

    std::set<std::string_view> parameterIds;
    for (const auto& parameter : preset.parameters) {
        if (parameter.id.empty())
            return "a parameter id must not be empty";
        if (!std::isfinite(parameter.value))
            return "parameter \"" + parameter.id + "\" is not finite";
        if (!parameterIds.insert(parameter.id).second)
            return "parameter \"" + parameter.id + "\" is repeated";
    }

    if (const auto* magda = std::get_if<PresetMagdaDevice>(&preset.device)) {
        if (magda->state.deviceType != preset.deviceType)
            return "the device state's \"device\" must equal \"deviceType\"";
    } else {
        const auto& plugin = std::get<PresetPluginDevice>(preset.device);
        if (plugin.format.empty())
            return "a plugin device needs a \"format\"";
        if (plugin.vst3ClassId && plugin.vst3ClassId->empty())
            return "\"vst3ClassId\" must not be empty";
        if (plugin.chunk.size() > kMaxBinaryBytes ||
            (plugin.vst3Preset && plugin.vst3Preset->size() > kMaxBinaryBytes))
            return "a plugin device's state exceeds the limit";
    }

    std::set<std::string_view> keys;
    for (const auto& asset : preset.assets) {
        if (asset.key.empty())
            return "an asset key must not be empty";
        if (!keys.insert(asset.key).second)
            return "asset \"" + asset.key + "\" is repeated";
        if (!isValidAssetPath(asset.path))
            return "asset \"" + asset.key + "\" has an invalid path";
        if (asset.sha256 && !isLowerHex(*asset.sha256, 64))
            return "asset \"" + asset.key + "\" has an invalid sha256";
    }

    if (preset.host) {
        std::string error;
        const auto parsed = detail::parseJson(*preset.host, error, kMaxPresetJsonDepth);
        if (!parsed || parsed->type != JsonValue::Type::Object)
            return "\"host\" must be one JSON object";
    }
    return std::nullopt;
}

PresetReadResult refuse(PresetStatus status, std::string message) {
    PresetReadResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

PresetReadResult invalid(std::string message) {
    return refuse(PresetStatus::Invalid, std::move(message));
}

const char* domainName(PresetValueDomain domain) {
    return domain == PresetValueDomain::Display ? "display" : "normalized";
}

bool readString(const JsonValue& object, std::string_view key, std::string& out, std::string& error,
                bool required = true) {
    const auto* value = object.member(key);
    if (value == nullptr) {
        if (required)
            error = "missing \"" + std::string(key) + "\"";
        return !required;
    }
    if (value->type != JsonValue::Type::String) {
        error = "\"" + std::string(key) + "\" must be a string";
        return false;
    }
    out = value->string;
    return true;
}

bool onlyMembers(const JsonValue& object, std::initializer_list<std::string_view> allowed,
                 std::string& error) {
    for (const auto& [name, value] : object.object) {
        if (std::ranges::find(allowed, name) == allowed.end()) {
            error = "unknown member \"" + name + "\"";
            return false;
        }
    }
    return true;
}

bool readBinary(const JsonValue& object, std::string_view key, Binary& out, std::string& error) {
    const auto* value = object.member(key);
    if (value == nullptr || value->type != JsonValue::Type::String) {
        error = "\"" + std::string(key) + "\" must be a base64 string";
        return false;
    }
    auto bytes = decodeBase64(value->string);
    if (!bytes) {
        error = "\"" + std::string(key) + "\" is not standard base64";
        return false;
    }
    out = std::move(*bytes);
    return true;
}

PresetReadResult readDevice(const JsonValue& json, Preset& preset) {
    if (json.type != JsonValue::Type::Object)
        return invalid("\"device\" must be an object");

    std::string error;
    std::string kind;
    if (!readString(json, "kind", kind, error))
        return invalid(error);

    if (kind == "magda") {
        if (!onlyMembers(json, {"kind", "state"}, error))
            return invalid("device: " + error);
        const auto* state = json.member("state");
        if (state == nullptr)
            return invalid("a magda device needs a \"state\"");
        auto decoded = detail::decodeDocumentValue(*state);
        if (decoded.status == DecodeStatus::FutureSchema)
            return refuse(PresetStatus::FutureVersion, "device state: " + decoded.message);
        if (!decoded.ok())
            return invalid("device state: " + decoded.message);
        preset.device = PresetMagdaDevice{std::move(*decoded.document)};
        return {PresetStatus::Ok, {}, std::nullopt};
    }

    if (kind == "plugin") {
        if (!onlyMembers(json,
                         {"kind", "format", "uniqueId", "fileOrIdentifier", "vst3ClassId", "chunk",
                          "vst3Preset"},
                         error))
            return invalid("device: " + error);

        PresetPluginDevice plugin;
        if (!readString(json, "format", plugin.format, error) ||
            !readString(json, "uniqueId", plugin.uniqueId, error) ||
            !readString(json, "fileOrIdentifier", plugin.fileOrIdentifier, error) ||
            !readBinary(json, "chunk", plugin.chunk, error))
            return invalid("device: " + error);

        if (json.member("vst3ClassId") != nullptr) {
            std::string classId;
            if (!readString(json, "vst3ClassId", classId, error))
                return invalid("device: " + error);
            plugin.vst3ClassId = std::move(classId);
        }
        if (json.member("vst3Preset") != nullptr) {
            Binary bytes;
            if (!readBinary(json, "vst3Preset", bytes, error))
                return invalid("device: " + error);
            plugin.vst3Preset = std::move(bytes);
        }
        preset.device = std::move(plugin);
        return {PresetStatus::Ok, {}, std::nullopt};
    }

    return invalid("device \"kind\" must be \"magda\" or \"plugin\"");
}

void appendKey(std::string& out, std::string_view key, int indent) {
    out.push_back('\n');
    out.append(static_cast<std::size_t>(indent) * 2, ' ');
    out.push_back('"');
    out += key;
    out += "\": ";
}

bool appendQuoted(std::string& out, std::string_view text, std::string& error) {
    if (!detail::appendJsonString(out, text)) {
        error = "text is not valid UTF-8";
        return false;
    }
    return true;
}

/// @p text with every line after the first moved in by @p indent levels.
std::string indented(std::string text, int indent) {
    while (!text.empty() && text.back() == '\n')
        text.pop_back();
    const std::string pad(static_cast<std::size_t>(indent) * 2, ' ');
    std::string out;
    for (const char c : text) {
        out.push_back(c);
        if (c == '\n')
            out += pad;
    }
    return out;
}

}  // namespace

bool isValidAssetPath(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.back() == '/' ||
        path.find('\\') != std::string_view::npos || !detail::isValidUtf8(path))
        return false;
    if (path.size() >= 2 && path[1] == ':')
        return false;

    std::size_t start = 0;
    while (start <= path.size()) {
        const auto slash = path.find('/', start);
        const auto segment =
            path.substr(start, slash == std::string_view::npos ? slash : slash - start);
        if (segment.empty() || segment == ".")
            return false;
        if (std::ranges::any_of(segment,
                                [](char c) { return static_cast<unsigned char>(c) < 0x20; }))
            return false;
        if (slash == std::string_view::npos)
            break;
        start = slash + 1;
    }
    return true;
}

PresetReadResult readPreset(std::string_view json) {
    std::string error;
    const auto parsed = detail::parseJson(json, error, kMaxPresetJsonDepth);
    if (!parsed)
        return refuse(PresetStatus::NotJson, error);

    const auto* format = parsed->member("format");
    if (parsed->type != JsonValue::Type::Object || format == nullptr ||
        format->type != JsonValue::Type::String || format->string != kPresetFormat)
        return refuse(PresetStatus::NotAPreset, "not a magda.preset document");

    const auto* version = parsed->member("version");
    if (version == nullptr || version->type != JsonValue::Type::Int)
        return invalid("\"version\" must be an integer");
    if (version->integer < 1)
        return refuse(PresetStatus::UnsupportedVersion, "version is older than this build reads");
    if (version->integer > kPresetVersion)
        return refuse(PresetStatus::FutureVersion, "version is newer than this build reads");

    if (!onlyMembers(*parsed,
                     {"format", "version", "id", "writer", "deviceType", "deviceVersion", "name",
                      "author", "tags", "created", "valueDomain", "parameters", "device", "assets",
                      "host"},
                     error))
        return invalid(error);

    Preset preset;
    if (!readString(*parsed, "id", preset.id, error) ||
        !readString(*parsed, "writer", preset.writer, error) ||
        !readString(*parsed, "deviceType", preset.deviceType, error) ||
        !readString(*parsed, "name", preset.name, error) ||
        !readString(*parsed, "author", preset.author, error, false) ||
        !readString(*parsed, "created", preset.created, error))
        return invalid(error);
    if (parsed->member("author") != nullptr && preset.author.empty())
        return invalid("\"author\" must be non-empty, or absent");

    const auto* deviceVersion = parsed->member("deviceVersion");
    if (deviceVersion == nullptr || deviceVersion->type != JsonValue::Type::Int ||
        deviceVersion->integer < 1 || deviceVersion->integer > 0x7FFFFFFF)
        return invalid("\"deviceVersion\" must be a positive integer");
    preset.deviceVersion = static_cast<int>(deviceVersion->integer);

    if (const auto* tags = parsed->member("tags")) {
        if (tags->type != JsonValue::Type::Array || tags->array.empty())
            return invalid("\"tags\" must be a non-empty array, or absent");
        for (const auto& tag : tags->array) {
            if (tag.type != JsonValue::Type::String)
                return invalid("a tag must be a string");
            preset.tags.push_back(tag.string);
        }
    }

    const auto* domain = parsed->member("valueDomain");
    if (domain == nullptr || domain->type != JsonValue::Type::String ||
        (domain->string != "display" && domain->string != "normalized"))
        return invalid("\"valueDomain\" must be \"display\" or \"normalized\"");
    preset.valueDomain =
        domain->string == "display" ? PresetValueDomain::Display : PresetValueDomain::Normalized;

    const auto* parameters = parsed->member("parameters");
    if (parameters == nullptr || parameters->type != JsonValue::Type::Object)
        return invalid("\"parameters\" must be an object");
    for (const auto& [id, value] : parameters->object) {
        if (value.type != JsonValue::Type::Int && value.type != JsonValue::Type::Double)
            return invalid("parameter \"" + id + "\" must be a number");
        preset.parameters.push_back({id, value.type == JsonValue::Type::Int
                                             ? static_cast<double>(value.integer)
                                             : value.real});
    }

    const auto* device = parsed->member("device");
    if (device == nullptr)
        return invalid("missing \"device\"");
    if (auto result = readDevice(*device, preset); result.status != PresetStatus::Ok)
        return result;

    if (const auto* assets = parsed->member("assets")) {
        if (assets->type != JsonValue::Type::Array || assets->array.empty())
            return invalid("\"assets\" must be a non-empty array, or absent");
        for (const auto& entry : assets->array) {
            if (entry.type != JsonValue::Type::Object)
                return invalid("an asset must be an object");
            if (!onlyMembers(entry, {"key", "path", "sha256"}, error))
                return invalid("asset: " + error);
            PresetAsset asset;
            if (!readString(entry, "key", asset.key, error) ||
                !readString(entry, "path", asset.path, error))
                return invalid("asset: " + error);
            if (entry.member("sha256") != nullptr) {
                std::string digest;
                if (!readString(entry, "sha256", digest, error))
                    return invalid("asset: " + error);
                asset.sha256 = std::move(digest);
            }
            preset.assets.push_back(std::move(asset));
        }
    }

    if (const auto* host = parsed->member("host")) {
        if (host->type != JsonValue::Type::Object)
            return invalid("\"host\" must be an object");
        std::string text;
        if (!detail::appendJson(text, *host, -1, error))
            return invalid("host: " + error);
        preset.host = std::move(text);
    }

    if (const auto broken = checkPreset(preset))
        return invalid(*broken);

    PresetReadResult result;
    result.status = PresetStatus::Ok;
    result.preset = std::move(preset);
    return result;
}

std::optional<std::string> writePreset(const Preset& preset, std::string& error) {
    if (const auto broken = checkPreset(preset)) {
        error = *broken;
        return std::nullopt;
    }

    std::string out = "{";
    const auto key = [&out](std::string_view name, bool first = false) {
        if (!first)
            out.push_back(',');
        appendKey(out, name, 1);
    };

    key("format", true);
    out += "\"" + std::string(kPresetFormat) + "\"";
    key("version");
    out += std::to_string(kPresetVersion);

    key("id");
    if (!appendQuoted(out, preset.id, error))
        return std::nullopt;
    key("writer");
    if (!appendQuoted(out, preset.writer, error))
        return std::nullopt;
    key("deviceType");
    if (!appendQuoted(out, preset.deviceType, error))
        return std::nullopt;
    key("deviceVersion");
    out += std::to_string(preset.deviceVersion);
    key("name");
    if (!appendQuoted(out, preset.name, error))
        return std::nullopt;
    if (!preset.author.empty()) {
        key("author");
        if (!appendQuoted(out, preset.author, error))
            return std::nullopt;
    }
    if (!preset.tags.empty()) {
        key("tags");
        out += "[";
        bool first = true;
        for (const auto& tag : preset.tags) {
            out += first ? "" : ", ";
            first = false;
            if (!appendQuoted(out, tag, error))
                return std::nullopt;
        }
        out += "]";
    }
    key("created");
    out += "\"" + preset.created + "\"";
    key("valueDomain");
    out += std::string("\"") + domainName(preset.valueDomain) + "\"";

    key("parameters");
    if (preset.parameters.empty()) {
        out += "{}";
    } else {
        out += "{";
        bool first = true;
        for (const auto& parameter : preset.parameters) {
            out += first ? "" : ",";
            first = false;
            out += "\n    ";
            if (!appendQuoted(out, parameter.id, error))
                return std::nullopt;
            out += ": " + detail::writeDouble(parameter.value);
        }
        out += "\n  }";
    }

    key("device");
    out += "{";
    if (const auto* magda = std::get_if<PresetMagdaDevice>(&preset.device)) {
        appendKey(out, "kind", 2);
        out += "\"magda\",";
        appendKey(out, "state", 2);
        const auto state = encodeDocument(magda->state, &error);
        if (!state)
            return std::nullopt;
        out += indented(*state, 2);
    } else {
        const auto& plugin = std::get<PresetPluginDevice>(preset.device);
        appendKey(out, "kind", 2);
        out += "\"plugin\",";
        appendKey(out, "format", 2);
        if (!appendQuoted(out, plugin.format, error))
            return std::nullopt;
        out += ",";
        appendKey(out, "uniqueId", 2);
        if (!appendQuoted(out, plugin.uniqueId, error))
            return std::nullopt;
        out += ",";
        appendKey(out, "fileOrIdentifier", 2);
        if (!appendQuoted(out, plugin.fileOrIdentifier, error))
            return std::nullopt;
        if (plugin.vst3ClassId) {
            out += ",";
            appendKey(out, "vst3ClassId", 2);
            if (!appendQuoted(out, *plugin.vst3ClassId, error))
                return std::nullopt;
        }
        out += ",";
        appendKey(out, "chunk", 2);
        out += "\"" + encodeBase64(plugin.chunk) + "\"";
        if (plugin.vst3Preset) {
            out += ",";
            appendKey(out, "vst3Preset", 2);
            out += "\"" + encodeBase64(*plugin.vst3Preset) + "\"";
        }
    }
    out += "\n  }";

    if (!preset.assets.empty()) {
        key("assets");
        out += "[";
        bool first = true;
        for (const auto& asset : preset.assets) {
            out += first ? "" : ",";
            first = false;
            out += "\n    {";
            appendKey(out, "key", 3);
            if (!appendQuoted(out, asset.key, error))
                return std::nullopt;
            out += ",";
            appendKey(out, "path", 3);
            if (!appendQuoted(out, asset.path, error))
                return std::nullopt;
            if (asset.sha256) {
                out += ",";
                appendKey(out, "sha256", 3);
                out += "\"" + *asset.sha256 + "\"";
            }
            out += "\n    }";
        }
        out += "\n  ]";
    }

    if (preset.host) {
        key("host");
        const auto parsed = detail::parseJson(*preset.host, error, kMaxPresetJsonDepth);
        std::string text;
        if (!parsed || !detail::appendJson(text, *parsed, 1, error))
            return std::nullopt;
        out += text;
    }

    out += "\n}\n";
    return out;
}

bool isFuturePreset(std::string_view json) {
    std::string error;
    const auto parsed = detail::parseJson(json, error, kMaxPresetJsonDepth);
    if (!parsed || parsed->type != JsonValue::Type::Object)
        return false;
    const auto* format = parsed->member("format");
    const auto* version = parsed->member("version");
    return format != nullptr && format->type == JsonValue::Type::String &&
           format->string == kPresetFormat && version != nullptr &&
           version->type == JsonValue::Type::Int && version->integer > kPresetVersion;
}

const ResolvedAsset* AssetResolution::find(std::string_view key) const {
    const auto it = std::ranges::find(assets, key, &ResolvedAsset::key);
    return it == assets.end() ? nullptr : &*it;
}

bool AssetResolution::complete() const {
    return std::ranges::all_of(assets, [](const ResolvedAsset& asset) {
        return asset.lookup.status == AssetLookup::Status::Found;
    });
}

std::vector<std::string> AssetResolution::unresolvedKeys() const {
    std::vector<std::string> keys;
    for (const auto& asset : assets)
        if (asset.lookup.status != AssetLookup::Status::Found)
            keys.push_back(asset.key);
    return keys;
}

AssetResolution resolveAssets(const Preset& preset, const AssetResolver& resolver) {
    AssetResolution resolution;
    resolution.assets.reserve(preset.assets.size());
    for (const auto& asset : preset.assets)
        resolution.assets.push_back({asset.key, resolver ? resolver(asset) : AssetLookup{}});
    return resolution;
}

}  // namespace magda::sdk
