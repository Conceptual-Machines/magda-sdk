#pragma once

#include <cmath>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace magda::sdk {

/// How a normalized position in [0, 1] maps to a real value (docs/parameter-manifest.md).
enum class ParameterScale {
    Linear,
    Logarithmic,
    Exponential,
    Discrete,
    Boolean,
    FaderDB,
};

/// The domain of ParameterDescriptor::defaultValue.
enum class ParameterValueConvention {
    Real,
    Normalized,
};

/// How a real value is shown and parsed. Default dispatches on the unit.
enum class DisplayFormat {
    Default,
    Decibels,
    Pan,
    Percent,
    MidiNote,
    Beats,
    BarsBeats,
};

/// The semantic role of a wrapper-injected parameter; None for every other.
enum class WrapperRole {
    None,
    DryGain,
    WetGain,
};

/// Whether a device's parameter set is fixed by the device or is part of its state.
enum class ParameterSource {
    Static,
    State,
};

/// One entry of a discrete parameter. Its real value is its position in the choice list.
struct ParameterChoice {
    /// Stable across versions and unique within the parameter; the cross-host key.
    std::string id;
    std::string label;

    /// What the choice stands for in the underlying plugin (a Faust menu value). Not a position.
    float value = 0.0f;

    bool operator==(const ParameterChoice&) const = default;
};

/// A labelled tick on an automation axis, at a real value.
struct ParameterLabelTick {
    float value = 0.0f;
    std::string label;

    bool operator==(const ParameterLabelTick&) const = default;
};

/**
 * @brief A parameter described statically: what a host needs to show, store and map it.
 *
 * JUCE-free. Fields a scale kind does not use are ignored and are not written to a manifest.
 */
struct ParameterDescriptor {
    /// Unique per device, the cross-host key. Empty derives `<pluginId>_param_<index>`.
    std::string stableId;

    /// The frozen slot. Negative means the declaration order.
    int index = -1;
    std::string name;
    std::string unit;

    /// UI page name; empty for the default page.
    std::string group;
    std::string tooltip;

    /// Grid cells the parameter asks for; the layout clamps it.
    int widthCells = 1;

    /// Range in real units.
    float minValue = 0.0f;
    float maxValue = 1.0f;
    ParameterValueConvention valueConvention = ParameterValueConvention::Real;

    /// In the domain valueConvention names.
    float defaultValue = 0.5f;

    ParameterScale scale = ParameterScale::Linear;

    /// Linear and Logarithmic: the real value placed at 0.5. Used only when strictly inside the
    /// range; 0 is the unset spelling.
    float scaleAnchor = 0.0f;

    /// Exponential: real = min + (max - min) * normalized^exponent.
    float exponent = 1.0f;

    /// FaderDB: the normalized position of unityDb.
    float unityPosition = 0.75f;
    float unityDb = 0.0f;

    /// Quantisation in real units; 0 is continuous.
    float step = 0.0f;

    /// Discrete: ordered. Real values are positions in this list.
    std::vector<ParameterChoice> choices;

    /// Discrete: a request to show the choices as a row of buttons.
    bool radioChoices = false;

    /// Automation axis labels; empty strides through the choices.
    std::vector<ParameterLabelTick> labelTicks;

    DisplayFormat displayFormat = DisplayFormat::Default;

    /// Dimmed unless slot @p gateSlotIndex is on (or off, when gateNegated); negative is none.
    int gateSlotIndex = -1;
    bool gateNegated = false;

    /// Addressable, but omitted from parameter grids.
    bool hidden = false;

    /// A Boolean that is 1 while pressed and returns to 0.
    bool momentary = false;

    bool automatable = true;
    bool readOnly = false;
    bool modulatable = true;
    bool bipolarModulation = false;
    WrapperRole wrapperRole = WrapperRole::None;

    bool operator==(const ParameterDescriptor&) const = default;
};

/// Choices from labels: id is the label (suffixed with its position when repeated), value the
/// position.
std::vector<ParameterChoice> choicesFromLabels(std::initializer_list<std::string_view> labels);
std::vector<ParameterChoice> choicesFromLabels(const std::vector<std::string>& labels);

/// The id a device without an explicit one reports for @p index.
std::string derivedStableId(std::string_view pluginId, int index);

/// @p descriptor with its index and stable id settled: @p slot and the derived id fill the unset.
ParameterDescriptor resolveDescriptor(ParameterDescriptor descriptor, std::string_view pluginId,
                                      int slot);

/// What decides a normalized position's meaning; a flat value fit for the audio thread.
struct ParameterDomain {
    ParameterScale scale = ParameterScale::Linear;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float scaleAnchor = 0.0f;
    float exponent = 1.0f;
    float unityPosition = 0.75f;
    float unityDb = 0.0f;

    /// Discrete only; zero converts to 0.
    int choiceCount = 0;

    bool operator==(const ParameterDomain&) const = default;
};

ParameterDomain domainOf(const ParameterDescriptor& descriptor);

/// Whether the anchor counts: strictly inside the range.
bool hasScaleAnchor(const ParameterDomain& domain);

/// Discrete and Boolean values are steps, never ramped.
bool isStepped(const ParameterDomain& domain);

/// Normalized (clamped to [0, 1]) to real units.
float normalizedToReal(float normalized, const ParameterDomain& domain);

/// Real units to normalized, in [0, 1].
float realToNormalized(float real, const ParameterDomain& domain);

}  // namespace magda::sdk
