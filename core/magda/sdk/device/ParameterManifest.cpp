#include "magda/sdk/device/ParameterManifest.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <span>

#include "magda/sdk/state/detail/Json.hpp"
#include "magda/sdk/state/detail/NumberText.hpp"

namespace magda::sdk {

namespace {

using detail::JsonValue;

struct NamedValue {
    const char* name;
    int value;
};

constexpr NamedValue kScales[] = {
    {"linear", static_cast<int>(ParameterScale::Linear)},
    {"logarithmic", static_cast<int>(ParameterScale::Logarithmic)},
    {"exponential", static_cast<int>(ParameterScale::Exponential)},
    {"discrete", static_cast<int>(ParameterScale::Discrete)},
    {"boolean", static_cast<int>(ParameterScale::Boolean)},
    {"faderDb", static_cast<int>(ParameterScale::FaderDB)},
};

constexpr NamedValue kConventions[] = {
    {"real", static_cast<int>(ParameterValueConvention::Real)},
    {"normalized", static_cast<int>(ParameterValueConvention::Normalized)},
};

constexpr NamedValue kFormats[] = {
    {"default", static_cast<int>(DisplayFormat::Default)},
    {"decibels", static_cast<int>(DisplayFormat::Decibels)},
    {"pan", static_cast<int>(DisplayFormat::Pan)},
    {"percent", static_cast<int>(DisplayFormat::Percent)},
    {"midiNote", static_cast<int>(DisplayFormat::MidiNote)},
    {"beats", static_cast<int>(DisplayFormat::Beats)},
    {"barsBeats", static_cast<int>(DisplayFormat::BarsBeats)},
};

constexpr NamedValue kRoles[] = {
    {"none", static_cast<int>(WrapperRole::None)},
    {"dryGain", static_cast<int>(WrapperRole::DryGain)},
    {"wetGain", static_cast<int>(WrapperRole::WetGain)},
};

constexpr NamedValue kSources[] = {
    {"static", static_cast<int>(ParameterSource::Static)},
    {"state", static_cast<int>(ParameterSource::State)},
};

template <std::size_t N> const char* nameOf(const NamedValue (&table)[N], int value) {
    for (const auto& entry : table)
        if (entry.value == value)
            return entry.name;
    return nullptr;
}

template <std::size_t N>
std::optional<int> valueOf(const NamedValue (&table)[N], std::string_view name) {
    for (const auto& entry : table)
        if (name == entry.name)
            return entry.value;
    return std::nullopt;
}

/// Whether @p scale reads the anchor, the exponent, the unity point, or the choices.
bool usesAnchor(ParameterScale scale) {
    return scale == ParameterScale::Linear || scale == ParameterScale::Logarithmic;
}

class Writer {
  public:
    explicit Writer(std::string& error) : error_(error) {}

    bool write(const DeviceManifest& manifest, std::string& out) {
        if (!validate(manifest))
            return false;

        out += "{\"format\":";
        string(out, kManifestFormat);
        out += ",\"version\":" + std::to_string(kManifestVersion);
        out += ",\"deviceType\":";
        string(out, manifest.deviceType);
        out += ",\"deviceVersion\":" + std::to_string(manifest.deviceVersion);
        out += ",\"parameterSource\":";
        string(out, nameOf(kSources, static_cast<int>(manifest.parameterSource)));
        out += ",\"parameters\":[";
        for (std::size_t i = 0; i < manifest.parameters.size(); ++i) {
            if (i > 0)
                out += ",";
            parameter(out, manifest.parameters[i]);
        }
        out += "]}";
        return ok_;
    }

    bool writeParameter(const ParameterDescriptor& p, std::string& out) {
        if (!validateParameter(p))
            return false;
        parameter(out, p);
        return ok_;
    }

  private:
    bool fail(const std::string& message) {
        if (error_.empty())
            error_ = message;
        ok_ = false;
        return false;
    }

    void string(std::string& out, std::string_view text) {
        if (!detail::appendJsonString(out, text)) {
            fail("a string is not valid UTF-8");
            out += "\"\"";
        }
    }

    void key(std::string& out, std::string_view name, bool& first) {
        out += first ? "\"" : ",\"";
        out += name;
        out += "\":";
        first = false;
    }

    void number(std::string& out, float value) {
        out += detail::writeFloat(value);
    }

    bool finite(float value, const std::string& what) {
        return std::isfinite(value) || fail(what + " is not finite");
    }

    bool validateParameter(const ParameterDescriptor& p) {
        const std::string where = "parameter '" + p.stableId + "'";
        if (p.stableId.empty() || p.index < 0)
            return fail("a parameter is not resolved (stableId and index are required)");
        if (p.widthCells < 1)
            return fail(where + " has widthCells below 1");
        if (!finite(p.minValue, where + " min") || !finite(p.maxValue, where + " max") ||
            !finite(p.defaultValue, where + " default") || !finite(p.step, where + " step") ||
            !finite(p.scaleAnchor, where + " anchor") || !finite(p.exponent, where + " exponent") ||
            !finite(p.unityPosition, where + " unityPosition") ||
            !finite(p.unityDb, where + " unityDb"))
            return false;
        if (p.minValue > p.maxValue)
            return fail(where + " has min above max");
        if (p.step < 0.0f)
            return fail(where + " has a negative step");
        if (p.scale == ParameterScale::FaderDB &&
            !(p.unityPosition > 0.0f && p.unityPosition < 1.0f))
            return fail(where + " has a unityPosition outside (0, 1)");

        std::set<std::string> choiceIds;
        if (p.scale == ParameterScale::Discrete)
            for (const auto& choice : p.choices) {
                if (choice.id.empty() || !choiceIds.insert(choice.id).second)
                    return fail(where + " has an empty or repeated choice id");
                if (!finite(choice.value, where + " choice value"))
                    return false;
            }
        for (const auto& tick : p.labelTicks)
            if (!finite(tick.value, where + " label tick"))
                return false;
        return true;
    }

    bool validate(const DeviceManifest& manifest) {
        if (manifest.deviceType.empty())
            return fail("deviceType is empty");
        if (manifest.deviceVersion < 1)
            return fail("deviceVersion is below 1");

        std::set<std::string> ids;
        std::set<int> indices;
        for (const auto& p : manifest.parameters) {
            if (!validateParameter(p))
                return false;
            if (!ids.insert(p.stableId).second)
                return fail("stableId '" + p.stableId + "' is repeated");
            if (!indices.insert(p.index).second)
                return fail("index " + std::to_string(p.index) + " is repeated");
        }
        return true;
    }

    void scale(std::string& out, const ParameterDescriptor& p) {
        out += "{\"kind\":";
        string(out, nameOf(kScales, static_cast<int>(p.scale)));
        const auto domain = domainOf(p);
        if (usesAnchor(p.scale) && hasScaleAnchor(domain)) {
            out += ",\"anchor\":";
            number(out, p.scaleAnchor);
        }
        if (p.scale == ParameterScale::Exponential) {
            out += ",\"exponent\":";
            number(out, p.exponent);
        }
        if (p.scale == ParameterScale::FaderDB) {
            out += ",\"unityPosition\":";
            number(out, p.unityPosition);
            out += ",\"unityDb\":";
            number(out, p.unityDb);
        }
        if (p.scale == ParameterScale::Discrete) {
            out += ",\"choices\":[";
            for (std::size_t i = 0; i < p.choices.size(); ++i) {
                if (i > 0)
                    out += ",";
                out += "{\"id\":";
                string(out, p.choices[i].id);
                out += ",\"label\":";
                string(out, p.choices[i].label);
                out += ",\"value\":";
                number(out, p.choices[i].value);
                out += "}";
            }
            out += "]";
            if (p.radioChoices)
                out += ",\"radio\":true";
        }
        out += "}";
    }

    void parameter(std::string& out, const ParameterDescriptor& p) {
        bool first = true;
        out += "{";
        key(out, "id", first);
        string(out, p.stableId);
        key(out, "index", first);
        out += std::to_string(p.index);
        key(out, "name", first);
        string(out, p.name);
        if (!p.unit.empty()) {
            key(out, "unit", first);
            string(out, p.unit);
        }
        if (!p.group.empty()) {
            key(out, "group", first);
            string(out, p.group);
        }
        if (!p.tooltip.empty()) {
            key(out, "tooltip", first);
            string(out, p.tooltip);
        }
        if (p.widthCells != 1) {
            key(out, "widthCells", first);
            out += std::to_string(p.widthCells);
        }
        key(out, "min", first);
        number(out, p.minValue);
        key(out, "max", first);
        number(out, p.maxValue);
        key(out, "default", first);
        number(out, p.defaultValue);
        if (p.valueConvention != ParameterValueConvention::Real) {
            key(out, "valueConvention", first);
            string(out, nameOf(kConventions, static_cast<int>(p.valueConvention)));
        }
        key(out, "scale", first);
        scale(out, p);
        if (p.step != 0.0f) {
            key(out, "step", first);
            number(out, p.step);
        }
        if (p.displayFormat != DisplayFormat::Default) {
            key(out, "displayFormat", first);
            string(out, nameOf(kFormats, static_cast<int>(p.displayFormat)));
        }
        if (!p.labelTicks.empty()) {
            key(out, "labelTicks", first);
            out += "[";
            for (std::size_t i = 0; i < p.labelTicks.size(); ++i) {
                if (i > 0)
                    out += ",";
                out += "{\"value\":";
                number(out, p.labelTicks[i].value);
                out += ",\"label\":";
                string(out, p.labelTicks[i].label);
                out += "}";
            }
            out += "]";
        }
        if (p.gateSlotIndex >= 0) {
            key(out, "gate", first);
            out += "{\"slot\":" + std::to_string(p.gateSlotIndex) +
                   (p.gateNegated ? ",\"negated\":true}" : "}");
        }
        const auto flag = [&](const char* name, bool value, bool fallback) {
            if (value != fallback) {
                key(out, name, first);
                out += value ? "true" : "false";
            }
        };
        flag("hidden", p.hidden, false);
        flag("momentary", p.momentary, false);
        flag("automatable", p.automatable, true);
        flag("readOnly", p.readOnly, false);
        flag("modulatable", p.modulatable, true);
        flag("bipolarModulation", p.bipolarModulation, false);
        if (p.wrapperRole != WrapperRole::None) {
            key(out, "wrapperRole", first);
            string(out, nameOf(kRoles, static_cast<int>(p.wrapperRole)));
        }
        out += "}";
    }

    std::string& error_;
    bool ok_ = true;
};

class Reader {
  public:
    explicit Reader(std::string& error) : error_(error) {}

    std::optional<DeviceManifest> read(const JsonValue& root) {
        if (!object(root, "the manifest"))
            return std::nullopt;
        if (!allow(root,
                   {"format", "version", "deviceType", "deviceVersion", "parameterSource",
                    "parameters"},
                   "the manifest"))
            return std::nullopt;

        DeviceManifest manifest;
        std::string text;
        if (!requireString(root, "format", text) || text != kManifestFormat) {
            fail("format is not \"" + std::string(kManifestFormat) + "\"");
            return std::nullopt;
        }
        int version = 0;
        if (!requireInt(root, "version", version))
            return std::nullopt;
        if (version != kManifestVersion) {
            fail("version " + std::to_string(version) + " is not supported");
            return std::nullopt;
        }
        if (!requireString(root, "deviceType", manifest.deviceType) ||
            !requireInt(root, "deviceVersion", manifest.deviceVersion))
            return std::nullopt;
        if (manifest.deviceType.empty() || manifest.deviceVersion < 1) {
            fail("deviceType is empty or deviceVersion is below 1");
            return std::nullopt;
        }
        if (!requireString(root, "parameterSource", text))
            return std::nullopt;
        const auto source = valueOf(kSources, text);
        if (!source) {
            fail("parameterSource '" + text + "' is unknown");
            return std::nullopt;
        }
        manifest.parameterSource = static_cast<ParameterSource>(*source);

        const auto* list = root.member("parameters");
        if (list == nullptr || list->type != JsonValue::Type::Array) {
            fail("parameters is missing or not an array");
            return std::nullopt;
        }
        std::set<std::string> ids;
        std::set<int> indices;
        for (const auto& entry : list->array) {
            auto parameter = readParameter(entry);
            if (!parameter)
                return std::nullopt;
            if (!ids.insert(parameter->stableId).second) {
                fail("id '" + parameter->stableId + "' is repeated");
                return std::nullopt;
            }
            if (!indices.insert(parameter->index).second) {
                fail("index " + std::to_string(parameter->index) + " is repeated");
                return std::nullopt;
            }
            manifest.parameters.push_back(std::move(*parameter));
        }
        return manifest;
    }

  private:
    bool fail(const std::string& message) {
        if (error_.empty())
            error_ = message;
        return false;
    }

    bool object(const JsonValue& value, const std::string& what) {
        return value.type == JsonValue::Type::Object || fail(what + " is not an object");
    }

    bool allow(const JsonValue& value, std::initializer_list<std::string_view> keys,
               const std::string& what) {
        for (const auto& [name, member] : value.object) {
            (void)member;
            if (std::ranges::find(keys, std::string_view(name)) == keys.end())
                return fail(what + " has an unknown key '" + name + "'");
        }
        return true;
    }

    bool requireString(const JsonValue& value, std::string_view name, std::string& out) {
        const auto* member = value.member(name);
        if (member == nullptr || member->type != JsonValue::Type::String)
            return fail("'" + std::string(name) + "' is missing or not a string");
        out = member->string;
        return true;
    }

    bool optionalString(const JsonValue& value, std::string_view name, std::string& out) {
        return value.member(name) == nullptr || requireString(value, name, out);
    }

    bool requireInt(const JsonValue& value, std::string_view name, int& out) {
        const auto* member = value.member(name);
        if (member == nullptr || member->type != JsonValue::Type::Int ||
            member->integer < -2147483647 || member->integer > 2147483647)
            return fail("'" + std::string(name) + "' is missing or not an integer");
        out = static_cast<int>(member->integer);
        return true;
    }

    bool optionalInt(const JsonValue& value, std::string_view name, int& out) {
        return value.member(name) == nullptr || requireInt(value, name, out);
    }

    bool numberOf(const JsonValue& member, std::string_view name, float& out) {
        if (member.type != JsonValue::Type::Int && member.type != JsonValue::Type::Double)
            return fail("'" + std::string(name) + "' is not a number");
        const double wide =
            member.type == JsonValue::Type::Int ? static_cast<double>(member.integer) : member.real;
        out = static_cast<float>(wide);
        return std::isfinite(out) || fail("'" + std::string(name) + "' is outside float range");
    }

    bool requireNumber(const JsonValue& value, std::string_view name, float& out) {
        const auto* member = value.member(name);
        if (member == nullptr)
            return fail("'" + std::string(name) + "' is missing");
        return numberOf(*member, name, out);
    }

    bool optionalNumber(const JsonValue& value, std::string_view name, float& out) {
        const auto* member = value.member(name);
        return member == nullptr || numberOf(*member, name, out);
    }

    bool optionalBool(const JsonValue& value, std::string_view name, bool& out) {
        const auto* member = value.member(name);
        if (member == nullptr)
            return true;
        if (member->type != JsonValue::Type::Bool)
            return fail("'" + std::string(name) + "' is not a boolean");
        out = member->boolean;
        return true;
    }

    template <std::size_t N>
    bool optionalNamed(const JsonValue& value, std::string_view name, const NamedValue (&table)[N],
                       int& out) {
        std::string text;
        if (value.member(name) == nullptr)
            return true;
        if (!requireString(value, name, text))
            return false;
        const auto found = valueOf(table, text);
        if (!found)
            return fail("'" + std::string(name) + "' value '" + text + "' is unknown");
        out = *found;
        return true;
    }

    bool readScale(const JsonValue& value, ParameterDescriptor& p) {
        const auto* scale = value.member("scale");
        if (scale == nullptr || !object(*scale, "scale"))
            return fail("'scale' is missing");

        std::string kind;
        if (!requireString(*scale, "kind", kind))
            return false;
        const auto found = valueOf(kScales, kind);
        if (!found)
            return fail("scale kind '" + kind + "' is unknown");
        p.scale = static_cast<ParameterScale>(*found);

        std::vector<std::string_view> keys{"kind"};
        if (usesAnchor(p.scale))
            keys.push_back("anchor");
        if (p.scale == ParameterScale::Exponential)
            keys.push_back("exponent");
        if (p.scale == ParameterScale::FaderDB) {
            keys.push_back("unityPosition");
            keys.push_back("unityDb");
        }
        if (p.scale == ParameterScale::Discrete) {
            keys.push_back("choices");
            keys.push_back("radio");
        }
        for (const auto& [name, member] : scale->object) {
            (void)member;
            if (std::ranges::find(keys, std::string_view(name)) == keys.end())
                return fail("scale '" + kind + "' does not take '" + name + "'");
        }

        if (!optionalNumber(*scale, "anchor", p.scaleAnchor) ||
            !optionalNumber(*scale, "exponent", p.exponent) ||
            !optionalNumber(*scale, "unityPosition", p.unityPosition) ||
            !optionalNumber(*scale, "unityDb", p.unityDb) ||
            !optionalBool(*scale, "radio", p.radioChoices))
            return false;

        if (usesAnchor(p.scale) && scale->member("anchor") != nullptr &&
            !hasScaleAnchor(domainOf(p)))
            return fail("anchor is not strictly inside the range");
        if (p.scale == ParameterScale::FaderDB &&
            !(p.unityPosition > 0.0f && p.unityPosition < 1.0f))
            return fail("unityPosition is outside (0, 1)");

        if (p.scale != ParameterScale::Discrete)
            return true;

        const auto* choices = scale->member("choices");
        if (choices == nullptr || choices->type != JsonValue::Type::Array)
            return fail("a discrete scale needs a 'choices' array");
        std::set<std::string> ids;
        for (const auto& entry : choices->array) {
            if (!object(entry, "a choice") || !allow(entry, {"id", "label", "value"}, "a choice"))
                return false;
            ParameterChoice choice;
            if (!requireString(entry, "id", choice.id) ||
                !requireString(entry, "label", choice.label) ||
                !requireNumber(entry, "value", choice.value))
                return false;
            if (choice.id.empty() || !ids.insert(choice.id).second)
                return fail("a choice id is empty or repeated");
            p.choices.push_back(std::move(choice));
        }
        return true;
    }

    std::optional<ParameterDescriptor> readParameter(const JsonValue& value) {
        if (!object(value, "a parameter"))
            return std::nullopt;
        if (!allow(value,
                   {"id",
                    "index",
                    "name",
                    "unit",
                    "group",
                    "tooltip",
                    "widthCells",
                    "min",
                    "max",
                    "default",
                    "valueConvention",
                    "scale",
                    "step",
                    "displayFormat",
                    "labelTicks",
                    "gate",
                    "hidden",
                    "momentary",
                    "automatable",
                    "readOnly",
                    "modulatable",
                    "bipolarModulation",
                    "wrapperRole"},
                   "a parameter"))
            return std::nullopt;

        ParameterDescriptor p;
        int convention = static_cast<int>(p.valueConvention);
        int format = static_cast<int>(p.displayFormat);
        int role = static_cast<int>(p.wrapperRole);
        if (!requireString(value, "id", p.stableId) || !requireInt(value, "index", p.index) ||
            !requireString(value, "name", p.name) || !optionalString(value, "unit", p.unit) ||
            !optionalString(value, "group", p.group) ||
            !optionalString(value, "tooltip", p.tooltip) ||
            !optionalInt(value, "widthCells", p.widthCells) ||
            !requireNumber(value, "min", p.minValue) || !requireNumber(value, "max", p.maxValue) ||
            !requireNumber(value, "default", p.defaultValue) ||
            !optionalNamed(value, "valueConvention", kConventions, convention) ||
            !readScale(value, p) || !optionalNumber(value, "step", p.step) ||
            !optionalNamed(value, "displayFormat", kFormats, format) ||
            !optionalBool(value, "hidden", p.hidden) ||
            !optionalBool(value, "momentary", p.momentary) ||
            !optionalBool(value, "automatable", p.automatable) ||
            !optionalBool(value, "readOnly", p.readOnly) ||
            !optionalBool(value, "modulatable", p.modulatable) ||
            !optionalBool(value, "bipolarModulation", p.bipolarModulation) ||
            !optionalNamed(value, "wrapperRole", kRoles, role))
            return std::nullopt;
        p.valueConvention = static_cast<ParameterValueConvention>(convention);
        p.displayFormat = static_cast<DisplayFormat>(format);
        p.wrapperRole = static_cast<WrapperRole>(role);

        if (p.stableId.empty() || p.index < 0 || p.widthCells < 1 || p.minValue > p.maxValue ||
            p.step < 0.0f) {
            fail("parameter '" + p.stableId +
                 "' has an empty id, a negative index, a width below "
                 "1, min above max or a negative step");
            return std::nullopt;
        }

        if (const auto* ticks = value.member("labelTicks")) {
            if (ticks->type != JsonValue::Type::Array) {
                fail("'labelTicks' is not an array");
                return std::nullopt;
            }
            for (const auto& entry : ticks->array) {
                ParameterLabelTick tick;
                if (!object(entry, "a label tick") || !allow(entry, {"value", "label"}, "a tick") ||
                    !requireNumber(entry, "value", tick.value) ||
                    !requireString(entry, "label", tick.label))
                    return std::nullopt;
                p.labelTicks.push_back(std::move(tick));
            }
        }

        if (const auto* gate = value.member("gate")) {
            if (!object(*gate, "'gate'") || !allow(*gate, {"slot", "negated"}, "'gate'") ||
                !requireInt(*gate, "slot", p.gateSlotIndex) ||
                !optionalBool(*gate, "negated", p.gateNegated))
                return std::nullopt;
            if (p.gateSlotIndex < 0) {
                fail("gate slot is negative");
                return std::nullopt;
            }
        }
        return p;
    }

    std::string& error_;
};

}  // namespace

DeviceManifest buildManifest(const Device& device) {
    const auto properties = device.properties();

    DeviceManifest manifest;
    manifest.deviceType = properties.pluginId;
    manifest.deviceVersion = properties.deviceVersion;
    manifest.parameterSource = properties.parameterSource;
    const int count = std::max(0, device.parameterCount());
    manifest.parameters.reserve(static_cast<std::size_t>(count));
    for (int slot = 0; slot < count; ++slot)
        manifest.parameters.push_back(
            resolveDescriptor(device.parameterDescriptor(slot), properties.pluginId, slot));
    return manifest;
}

std::optional<std::string> writeManifest(const DeviceManifest& manifest, std::string& error) {
    std::string out;
    if (!Writer(error).write(manifest, out))
        return std::nullopt;
    return out;
}

std::optional<std::string> writeManifestParameter(const ParameterDescriptor& parameter,
                                                  std::string& error) {
    std::string out;
    if (!Writer(error).writeParameter(parameter, out))
        return std::nullopt;
    return out;
}

std::optional<DeviceManifest> readManifest(std::string_view json, std::string& error) {
    const auto parsed = detail::parseJson(json, error);
    if (!parsed)
        return std::nullopt;
    return Reader(error).read(*parsed);
}

}  // namespace magda::sdk
