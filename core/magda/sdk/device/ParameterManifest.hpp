#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "magda/sdk/device/Device.hpp"
#include "magda/sdk/device/ParameterDescriptor.hpp"

namespace magda::sdk {

/// The format and version strings a manifest carries.
inline constexpr std::string_view kManifestFormat = "magda.device-manifest";
inline constexpr int kManifestVersion = 1;

/**
 * @brief A device's parameters as a host reads them without instantiating it
 * (docs/parameter-manifest.md).
 *
 * Parameters are resolved: every index and stable id is set and unique, in the device's slot order.
 */
struct DeviceManifest {
    std::string deviceType;
    int deviceVersion = 1;
    ParameterSource parameterSource = ParameterSource::Static;
    std::vector<ParameterDescriptor> parameters;

    bool operator==(const DeviceManifest&) const = default;
};

/// The manifest of @p device: its properties and every slot's descriptor, resolved. Control thread.
DeviceManifest buildManifest(const Device& device);

/// Strict JSON for @p manifest. Nullopt, with @p error set, when it breaks the format's rules.
std::optional<std::string> writeManifest(const DeviceManifest& manifest, std::string& error);

/// One resolved parameter as it appears in a manifest's "parameters" array.
std::optional<std::string> writeManifestParameter(const ParameterDescriptor& parameter,
                                                  std::string& error);

/// Strict inverse of writeManifest. Nullopt, with @p error set, for anything the format refuses.
std::optional<DeviceManifest> readManifest(std::string_view json, std::string& error);

}  // namespace magda::sdk
