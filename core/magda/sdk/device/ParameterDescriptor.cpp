#include "magda/sdk/device/ParameterDescriptor.hpp"

#include <algorithm>

namespace magda::sdk {

namespace {

float limit(float lower, float upper, float value) {
    return value < lower ? lower : (upper < value ? upper : value);
}

/// The skew exponent that puts @p anchorPosition at 0.5; 1 for a degenerate or centred anchor.
float computeSkew(float anchorPosition) {
    if (anchorPosition <= 1e-6f || anchorPosition >= 1.0f - 1e-6f)
        return 1.0f;
    if (std::abs(anchorPosition - 0.5f) < 1e-6f)
        return 1.0f;
    return std::log(anchorPosition) / std::log(0.5f);
}

}  // namespace

std::vector<ParameterChoice> choicesFromLabels(std::initializer_list<std::string_view> labels) {
    std::vector<std::string> copy;
    copy.reserve(labels.size());
    for (const auto label : labels)
        copy.emplace_back(label);
    return choicesFromLabels(copy);
}

std::vector<ParameterChoice> choicesFromLabels(const std::vector<std::string>& labels) {
    std::vector<ParameterChoice> choices;
    choices.reserve(labels.size());
    for (std::size_t position = 0; position < labels.size(); ++position) {
        const auto& label = labels[position];
        const bool repeated = std::ranges::count(labels, label) > 1;
        choices.push_back({repeated ? label + "_" + std::to_string(position) : label, label,
                           static_cast<float>(position)});
    }
    return choices;
}

std::string derivedStableId(std::string_view pluginId, int index) {
    return std::string(pluginId) + "_param_" + std::to_string(index);
}

ParameterDescriptor resolveDescriptor(ParameterDescriptor descriptor, std::string_view pluginId,
                                      int slot) {
    if (descriptor.index < 0)
        descriptor.index = slot;
    if (descriptor.stableId.empty())
        descriptor.stableId = derivedStableId(pluginId, descriptor.index);
    return descriptor;
}

ParameterDomain domainOf(const ParameterDescriptor& descriptor) {
    ParameterDomain domain;
    domain.scale = descriptor.scale;
    domain.minValue = descriptor.minValue;
    domain.maxValue = descriptor.maxValue;
    domain.scaleAnchor = descriptor.scaleAnchor;
    domain.exponent = descriptor.exponent;
    domain.unityPosition = descriptor.unityPosition;
    domain.unityDb = descriptor.unityDb;
    domain.choiceCount = static_cast<int>(descriptor.choices.size());
    return domain;
}

bool hasScaleAnchor(const ParameterDomain& domain) {
    return domain.scaleAnchor > domain.minValue && domain.scaleAnchor < domain.maxValue;
}

bool isStepped(const ParameterDomain& domain) {
    return domain.scale == ParameterScale::Discrete || domain.scale == ParameterScale::Boolean;
}

float normalizedToReal(float normalized, const ParameterDomain& domain) {
    normalized = limit(0.0f, 1.0f, normalized);

    switch (domain.scale) {
        case ParameterScale::Linear: {
            float range = domain.maxValue - domain.minValue;
            if (hasScaleAnchor(domain) && range > 0.0f) {
                float anchorPos = (domain.scaleAnchor - domain.minValue) / range;
                float skew = computeSkew(anchorPos);
                normalized = std::pow(normalized, skew);
            }
            return domain.minValue + normalized * range;
        }

        case ParameterScale::Logarithmic: {
            // Log is undefined for a non-positive minimum, so it reads linearly.
            if (domain.minValue <= 0.0f)
                return domain.minValue + normalized * (domain.maxValue - domain.minValue);

            float logRange = std::log(domain.maxValue / domain.minValue);
            if (hasScaleAnchor(domain)) {
                float anchorLogPos = std::log(domain.scaleAnchor / domain.minValue) / logRange;
                float skew = computeSkew(anchorLogPos);
                normalized = std::pow(normalized, skew);
            }
            return domain.minValue * std::exp(normalized * logRange);
        }

        case ParameterScale::Exponential:
            return std::pow(normalized, domain.exponent) * (domain.maxValue - domain.minValue) +
                   domain.minValue;

        case ParameterScale::Discrete: {
            if (domain.choiceCount <= 0)
                return 0.0f;
            int index = static_cast<int>(std::round(normalized * (domain.choiceCount - 1)));
            return static_cast<float>(index);
        }

        case ParameterScale::Boolean:
            return normalized >= 0.5f ? 1.0f : 0.0f;

        case ParameterScale::FaderDB: {
            if (normalized <= 0.0f)
                return domain.minValue;
            if (normalized >= 1.0f)
                return domain.maxValue;

            if (normalized < domain.unityPosition)
                return domain.minValue +
                       (normalized / domain.unityPosition) * (domain.unityDb - domain.minValue);
            return domain.unityDb +
                   ((normalized - domain.unityPosition) / (1.0f - domain.unityPosition)) *
                       (domain.maxValue - domain.unityDb);
        }
    }
    return domain.minValue + normalized * (domain.maxValue - domain.minValue);
}

float realToNormalized(float real, const ParameterDomain& domain) {
    switch (domain.scale) {
        case ParameterScale::Linear: {
            float range = domain.maxValue - domain.minValue;
            if (range == 0.0f)
                return 0.0f;
            float linPos = (real - domain.minValue) / range;
            if (hasScaleAnchor(domain)) {
                float anchorPos = (domain.scaleAnchor - domain.minValue) / range;
                float skew = computeSkew(anchorPos);
                linPos = std::pow(limit(0.0f, 1.0f, linPos), 1.0f / skew);
            }
            return limit(0.0f, 1.0f, linPos);
        }

        case ParameterScale::Logarithmic: {
            if (domain.minValue <= 0.0f || real <= 0.0f) {
                float range = domain.maxValue - domain.minValue;
                if (range == 0.0f)
                    return 0.0f;
                return limit(0.0f, 1.0f, (real - domain.minValue) / range);
            }
            float logRange = std::log(domain.maxValue / domain.minValue);
            if (logRange == 0.0f)
                return 0.0f;
            float logPos = std::log(real / domain.minValue) / logRange;
            if (hasScaleAnchor(domain)) {
                float anchorLogPos = std::log(domain.scaleAnchor / domain.minValue) / logRange;
                float skew = computeSkew(anchorLogPos);
                logPos = std::pow(limit(0.0f, 1.0f, logPos), 1.0f / skew);
            }
            return limit(0.0f, 1.0f, logPos);
        }

        case ParameterScale::Exponential: {
            float range = domain.maxValue - domain.minValue;
            if (range == 0.0f || domain.exponent == 0.0f)
                return 0.0f;
            float normalized = (real - domain.minValue) / range;
            return limit(0.0f, 1.0f, std::pow(normalized, 1.0f / domain.exponent));
        }

        case ParameterScale::Discrete: {
            if (domain.choiceCount <= 0)
                return 0.0f;
            const float rounded = std::round(real);
            const int index = std::clamp(static_cast<int>(rounded), 0, domain.choiceCount - 1);
            return static_cast<float>(index) / static_cast<float>(domain.choiceCount - 1);
        }

        case ParameterScale::Boolean:
            return real >= 0.5f ? 1.0f : 0.0f;

        case ParameterScale::FaderDB: {
            if (real <= domain.minValue)
                return 0.0f;
            if (real >= domain.maxValue)
                return 1.0f;

            if (real < domain.unityDb)
                return domain.unityPosition * (real - domain.minValue) /
                       (domain.unityDb - domain.minValue);
            return domain.unityPosition + (1.0f - domain.unityPosition) * (real - domain.unityDb) /
                                              (domain.maxValue - domain.unityDb);
        }
    }

    float range = domain.maxValue - domain.minValue;
    if (range == 0.0f)
        return 0.0f;
    return limit(0.0f, 1.0f, (real - domain.minValue) / range);
}

}  // namespace magda::sdk
