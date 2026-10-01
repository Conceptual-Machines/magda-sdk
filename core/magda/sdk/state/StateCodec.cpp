#include "magda/sdk/state/StateCodec.hpp"

#include <limits>

#include "magda/sdk/state/BinaryText.hpp"
#include "magda/sdk/state/detail/Json.hpp"
#include "magda/sdk/state/detail/NumberText.hpp"

namespace magda::sdk {

namespace {

using detail::JsonValue;

constexpr std::string_view kBinaryKey = "$bin";

DecodeResult refuse(DecodeStatus status, std::string message) {
    DecodeResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

bool decodeValue(const JsonValue& json, StateValue& out, std::string& error) {
    switch (json.type) {
        case JsonValue::Type::Int:
            out = StateValue::fromInt(json.integer);
            return true;
        case JsonValue::Type::Double: {
            auto value = StateValue::fromDouble(json.real);
            if (!value) {
                error = "non-finite number";
                return false;
            }
            out = std::move(*value);
            return true;
        }
        case JsonValue::Type::Bool:
            out = StateValue::fromBool(json.boolean);
            return true;
        case JsonValue::Type::String:
            out = StateValue::fromString(json.string);
            return true;
        case JsonValue::Type::Object: {
            const auto* text = json.object.size() == 1 ? json.member(kBinaryKey) : nullptr;
            if (text == nullptr || text->type != JsonValue::Type::String) {
                error = "a property value that is an object must be {\"$bin\": \"<text>\"}";
                return false;
            }
            auto bytes = decodeBinaryText(text->string, &error);
            if (!bytes)
                return false;
            out = StateValue::fromBinary(std::move(*bytes));
            return true;
        }
        case JsonValue::Type::Array:
            error = "a property value cannot be an array";
            return false;
        case JsonValue::Type::Null:
            error = "a property value cannot be null";
            return false;
    }
    return false;
}

bool decodeNodeBody(const JsonValue& json, bool isRoot, int depth, StateNode& node,
                    std::string& error) {
    if (depth > kMaxStateDepth) {
        error = "nodes nest too deep";
        return false;
    }

    for (const auto& [key, value] : json.object) {
        if (key == "props") {
            if (value.type != JsonValue::Type::Object) {
                error = "\"props\" must be an object";
                return false;
            }
            for (const auto& [name, propertyJson] : value.object) {
                StateValue decoded = StateValue::fromBool(false);
                std::string why;
                if (name.empty()) {
                    error = "empty property key";
                    return false;
                }
                if (!decodeValue(propertyJson, decoded, why)) {
                    error = "property \"" + name + "\": " + why;
                    return false;
                }
                node.set(name, std::move(decoded));
            }
        } else if (key == "children") {
            if (value.type != JsonValue::Type::Array) {
                error = "\"children\" must be an array";
                return false;
            }
            for (const auto& childJson : value.array) {
                if (childJson.type != JsonValue::Type::Object) {
                    error = "a child must be an object";
                    return false;
                }
                const auto* type = childJson.member("type");
                if (type == nullptr || type->type != JsonValue::Type::String ||
                    type->string.empty()) {
                    error = "a child needs a non-empty \"type\"";
                    return false;
                }
                StateNode child(type->string);
                if (!decodeNodeBody(childJson, false, depth + 1, child, error))
                    return false;
                node.addChild(std::move(child));
            }
        } else if (isRoot ? (key == "schema" || key == "device") : key == "type") {
            continue;
        } else {
            error = "unknown member \"" + key + "\"";
            return false;
        }
    }
    return true;
}

bool appendNode(std::string& out, const StateNode& node, bool isRoot, int depth, int indent,
                std::string& error);

void newline(std::string& out, int indent) {
    out.push_back('\n');
    out.append(static_cast<std::size_t>(indent) * 2, ' ');
}

bool appendValue(std::string& out, const StateValue& value, std::string& error) {
    switch (value.kind()) {
        case StateValue::Kind::Int64:
            out += std::to_string(*value.int64());
            return true;
        case StateValue::Kind::Double:
            out += detail::writeDouble(*value.real());
            return true;
        case StateValue::Kind::Bool:
            out += *value.boolean() ? "true" : "false";
            return true;
        case StateValue::Kind::String:
            if (!detail::appendJsonString(out, *value.string())) {
                error = "string is not valid UTF-8";
                return false;
            }
            return true;
        case StateValue::Kind::Binary:
            if (value.binary()->size() > kMaxBinaryBytes) {
                error = "binary value exceeds the limit";
                return false;
            }
            out += "{\"$bin\": ";
            detail::appendJsonString(out, encodeBinaryText(*value.binary()));
            out += "}";
            return true;
    }
    return false;
}

bool appendNode(std::string& out, const StateNode& node, bool isRoot, int depth, int indent,
                std::string& error) {
    if (depth > kMaxStateDepth) {
        error = "nodes nest too deep";
        return false;
    }

    bool first = true;
    const auto member = [&](std::string_view name) {
        out += first ? "" : ",";
        first = false;
        newline(out, indent + 1);
        out += '"';
        out += name;
        out += "\": ";
    };

    if (!isRoot) {
        member("type");
        if (node.type().empty()) {
            error = "a child needs a non-empty type";
            return false;
        }
        if (!detail::appendJsonString(out, node.type())) {
            error = "type is not valid UTF-8";
            return false;
        }
    }

    if (!node.properties().empty()) {
        member("props");
        out += '{';
        bool firstProperty = true;
        for (const auto& property : node.properties()) {
            if (property.key.empty()) {
                error = "empty property key";
                return false;
            }
            out += firstProperty ? "" : ",";
            firstProperty = false;
            newline(out, indent + 2);
            if (!detail::appendJsonString(out, property.key)) {
                error = "property key is not valid UTF-8";
                return false;
            }
            out += ": ";
            if (!appendValue(out, property.value, error))
                return false;
        }
        newline(out, indent + 1);
        out += '}';
    }

    if (!node.children().empty()) {
        member("children");
        out += '[';
        bool firstChild = true;
        for (const auto& child : node.children()) {
            out += firstChild ? "" : ",";
            firstChild = false;
            newline(out, indent + 2);
            out += '{';
            if (!appendNode(out, child, false, depth + 1, indent + 2, error))
                return false;
            newline(out, indent + 2);
            out += '}';
        }
        newline(out, indent + 1);
        out += ']';
    }

    return true;
}

}  // namespace

DecodeResult decodeDocument(std::string_view json) {
    std::string error;
    const auto parsed = detail::parseJson(json, error, kMaxStateDepth * 2 + 8);
    if (!parsed)
        return refuse(DecodeStatus::NotJson, error);

    const auto* schema = parsed->member("schema");
    const auto* device = parsed->member("device");
    if (parsed->type != JsonValue::Type::Object || schema == nullptr || device == nullptr)
        return refuse(DecodeStatus::NotADocument, "not a device state document");

    if (schema->type != JsonValue::Type::Int)
        return refuse(DecodeStatus::Invalid, "\"schema\" must be an integer");
    if (schema->integer < kMinStateSchemaVersion)
        return refuse(DecodeStatus::UnsupportedSchema, "schema is older than this build reads");
    if (schema->integer > kStateSchemaVersion)
        return refuse(DecodeStatus::FutureSchema, "schema is newer than this build reads");

    if (device->type != JsonValue::Type::String || device->string.empty())
        return refuse(DecodeStatus::Invalid, "\"device\" must be a non-empty string");

    StateDocument document;
    document.schema = static_cast<int>(schema->integer);
    document.deviceType = device->string;

    if (!decodeNodeBody(*parsed, true, 1, document.root, error))
        return refuse(DecodeStatus::Invalid, error);

    DecodeResult result;
    result.status = DecodeStatus::Ok;
    result.document = std::move(document);
    return result;
}

std::optional<std::string> encodeDocument(const StateDocument& document, std::string* error) {
    std::string why;
    const auto refused = [&](const char* message) -> std::optional<std::string> {
        if (error != nullptr)
            *error = message;
        return std::nullopt;
    };

    if (document.schema != kStateSchemaVersion)
        return refused("only the current schema is written");
    if (document.deviceType.empty())
        return refused("empty device type");
    if (!document.root.type().empty())
        return refused("the root node has no type");

    std::string out = "{\n  \"schema\": " + std::to_string(document.schema) + ",\n  \"device\": ";
    if (!detail::appendJsonString(out, document.deviceType))
        return refused("device type is not valid UTF-8");

    std::string body;
    if (!appendNode(body, document.root, true, 1, 0, why)) {
        if (error != nullptr)
            *error = why;
        return std::nullopt;
    }

    // appendNode writes "," only between its own members, so one leads the body here.
    if (!body.empty())
        out += "," + body;
    out += "\n}\n";
    return out;
}

std::optional<int> schemaVersionOf(std::string_view json) {
    std::string error;
    const auto parsed = detail::parseJson(json, error, kMaxStateDepth * 2 + 8);
    if (!parsed || parsed->type != JsonValue::Type::Object)
        return std::nullopt;

    const auto* schema = parsed->member("schema");
    if (schema == nullptr || parsed->member("device") == nullptr ||
        schema->type != JsonValue::Type::Int)
        return std::nullopt;

    if (schema->integer < std::numeric_limits<int>::min() ||
        schema->integer > std::numeric_limits<int>::max())
        return std::nullopt;
    return static_cast<int>(schema->integer);
}

bool isFutureSchema(std::string_view json) {
    const auto version = schemaVersionOf(json);
    return version.has_value() && *version > kStateSchemaVersion;
}

}  // namespace magda::sdk
