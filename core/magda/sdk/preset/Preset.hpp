#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "magda/sdk/state/StateCodec.hpp"

namespace magda::sdk {

/// The format and version strings a preset carries (docs/preset.md).
inline constexpr std::string_view kPresetFormat = "magda.preset";
inline constexpr int kPresetVersion = 1;

/// Which domain `Preset::parameters` values are in.
enum class PresetValueDomain {
    /// The device's real units, as `ParameterDescriptor` ranges them.
    Display,
    /// Positions in [0, 1], for a hosted plugin whose parameters have no real units.
    Normalized,
};

/// A file the preset's state refers to by key instead of by absolute path.
struct PresetAsset {
    std::string key;

    /// Relative to the preset file, forward slashes, no leading slash (isValidAssetPath).
    std::string path;

    /// Lowercase hex of the file's SHA-256, when the writer knew it.
    std::optional<std::string> sha256;

    bool operator==(const PresetAsset&) const = default;
};

/// One parameter value, keyed by the descriptor's stable id.
struct PresetParameter {
    std::string id;
    double value = 0.0;

    bool operator==(const PresetParameter&) const = default;
};

/// A MAGDA device: its state document, restored through `Device::restoreState`.
struct PresetMagdaDevice {
    StateDocument state;

    bool operator==(const PresetMagdaDevice&) const = default;
};

/// A hosted plugin: how to find it and the state it saved.
struct PresetPluginDevice {
    std::string format;
    std::string uniqueId;
    std::string fileOrIdentifier;

    /// Portable VST3 class id (32 hex chars), when known.
    std::optional<std::string> vst3ClassId;

    /// The plugin's own state; empty when it saved none.
    Binary chunk;

    /// A Steinberg .vstpreset, when captured.
    std::optional<Binary> vst3Preset;

    bool operator==(const PresetPluginDevice&) const = default;
};

/**
 * @brief A portable device preset (docs/preset.md).
 *
 * `host` is one JSON object in canonical compact text. The SDK keeps it and writes it back
 * without reading what it means.
 */
struct Preset {
    /// Opaque and stable across overwrite and rename; a UUID for a new preset.
    std::string id;

    /// Free text naming the program that wrote the file, such as "MAGDA 1.0.0".
    std::string writer;

    std::string deviceType;
    int deviceVersion = 1;
    std::string name;
    std::string author;
    std::vector<std::string> tags;

    /// `YYYY-MM-DDTHH:MM:SSZ`.
    std::string created;

    PresetValueDomain valueDomain = PresetValueDomain::Display;
    std::vector<PresetParameter> parameters;
    std::variant<PresetMagdaDevice, PresetPluginDevice> device;
    std::vector<PresetAsset> assets;
    std::optional<std::string> host;

    bool operator==(const Preset&) const = default;
};

enum class PresetStatus {
    Ok,
    /// Not parseable JSON.
    NotJson,
    /// JSON, but not a `magda.preset` document.
    NotAPreset,
    /// A version below 1.
    UnsupportedVersion,
    /// A version newer than kPresetVersion, or a device state from a newer schema. Never rewrite
    /// the source (see isFuturePreset).
    FutureVersion,
    /// A version this build reads, with content the format does not allow.
    Invalid,
};

struct PresetReadResult {
    PresetStatus status = PresetStatus::NotJson;
    std::string message;
    std::optional<Preset> preset;

    bool ok() const {
        return status == PresetStatus::Ok;
    }
};

/// Parse a preset. Strict: it reads nothing it would not write, and an unknown member is refused
/// everywhere except inside `host`.
PresetReadResult readPreset(std::string_view json);

/// The canonical JSON text of @p preset. Nullopt, with @p error set, for one the reader would
/// refuse.
std::optional<std::string> writePreset(const Preset& preset, std::string& error);

/// Whether @p json is a `magda.preset` object whose version is newer than this build reads.
bool isFuturePreset(std::string_view json);

/// Whether @p path may be an asset path: relative, forward slashes, no empty or `.` segment.
bool isValidAssetPath(std::string_view path);

/// What a host found for one asset.
struct AssetLookup {
    enum class Status {
        Found,
        Missing,
        /// The file exists but its SHA-256 differs from `PresetAsset::sha256`. The host hashes.
        Mismatch,
    };

    Status status = Status::Missing;

    /// Where the host found it, in the host's own terms. Empty unless Found or Mismatch.
    std::string location;
};

using AssetResolver = std::function<AssetLookup(const PresetAsset&)>;

struct ResolvedAsset {
    std::string key;
    AssetLookup lookup;
};

/// What resolving a preset's assets came to, in the preset's order.
struct AssetResolution {
    std::vector<ResolvedAsset> assets;

    const ResolvedAsset* find(std::string_view key) const;

    /// True when every asset was Found.
    bool complete() const;

    /// Keys that were Missing or Mismatch, for the host to report.
    std::vector<std::string> unresolvedKeys() const;
};

/// Ask @p resolver about each of @p preset's assets.
AssetResolution resolveAssets(const Preset& preset, const AssetResolver& resolver);

}  // namespace magda::sdk
