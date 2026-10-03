#pragma once

#include <memory>
#include <span>
#include <string_view>

#include "magda/sdk/device/Device.hpp"

namespace magda::sdk::abi {

/// One device type a module exposes through the C ABI.
struct DeviceFactory {
    std::string_view deviceType;
    std::unique_ptr<Device> (*create)() = nullptr;
};

/**
 * @brief The devices this module exposes, in a fixed order.
 *
 * Not defined by the SDK: the program that links magda::sdk_abi defines it once, as it would main.
 */
std::span<const DeviceFactory> moduleDevices();

}  // namespace magda::sdk::abi
