#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace magda::sdk::display {

/// The semantic colours a UI core paints with; each shell maps them onto its theme.
enum class ColourRole : std::uint8_t {
    Background,
    Surface,
    Border,
    Text,
    TextDim,
    Accent,
    MeterLow,
    MeterMid,
    MeterHigh,
    MeterClip,
};

inline constexpr int kNumColourRoles = static_cast<int>(ColourRole::MeterClip) + 1;

/// The wire name, as docs/display-list.md lists it.
std::string_view colourRoleName(ColourRole role);
std::optional<ColourRole> colourRoleFromName(std::string_view name);

/// The SDK's reference palette, ARGB, for a shell with no theme of its own.
std::uint32_t defaultColour(ColourRole role);

}  // namespace magda::sdk::display
