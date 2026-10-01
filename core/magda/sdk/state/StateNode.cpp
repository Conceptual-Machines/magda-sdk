#include "magda/sdk/state/StateNode.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "magda/sdk/state/BinaryText.hpp"
#include "magda/sdk/state/detail/NumberText.hpp"

namespace magda::sdk {

namespace {

bool isSpace(char c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

std::string_view skipSpace(std::string_view text) {
    while (!text.empty() && isSpace(text.front()))
        text.remove_prefix(1);
    return text;
}

std::string_view trim(std::string_view text) {
    text = skipSpace(text);
    while (!text.empty() && isSpace(text.back()))
        text.remove_suffix(1);
    return text;
}

bool equalsIgnoreCase(std::string_view text, std::string_view lower) {
    return text.size() == lower.size() &&
           std::equal(text.begin(), text.end(), lower.begin(), [](char a, char b) {
               return (a >= 'A' && a <= 'Z' ? static_cast<char>(a - 'A' + 'a') : a) == b;
           });
}

/// juce::String::getIntValue / getLargeIntValue: leading blanks, an optional '-', then digits
/// up to the first other char; no '+', and overflow wraps.
template <typename Int, typename Unsigned>
Int readInteger(std::string_view text) {
    text = skipSpace(text);
    const bool negative = !text.empty() && text.front() == '-';
    if (negative)
        text.remove_prefix(1);

    Unsigned value = 0;
    for (const auto c : text) {
        if (c < '0' || c > '9')
            break;
        value = static_cast<Unsigned>(value * 10 + static_cast<Unsigned>(c - '0'));
    }

    return negative ? static_cast<Int>(Unsigned{0} - value) : static_cast<Int>(value);
}

template <typename Int>
Int saturatingCast(double value) {
    if (value >= static_cast<double>(std::numeric_limits<Int>::max()))
        return std::numeric_limits<Int>::max();
    if (value <= static_cast<double>(std::numeric_limits<Int>::min()))
        return std::numeric_limits<Int>::min();
    return static_cast<Int>(value);
}

}  // namespace

StateValue StateValue::fromInt(std::int64_t value) {
    return StateValue(Storage(std::in_place_index<0>, value));
}

StateValue StateValue::fromBool(bool value) {
    return StateValue(Storage(std::in_place_index<2>, value));
}

StateValue StateValue::fromString(std::string value) {
    return StateValue(Storage(std::in_place_index<3>, std::move(value)));
}

StateValue StateValue::fromBinary(Binary value) {
    return StateValue(Storage(std::in_place_index<4>, std::move(value)));
}

std::optional<StateValue> StateValue::fromDouble(double value) {
    if (!std::isfinite(value))
        return std::nullopt;
    return StateValue(Storage(std::in_place_index<1>, value));
}

StateValue::Kind StateValue::kind() const {
    return static_cast<Kind>(storage_.index());
}

const std::int64_t* StateValue::int64() const {
    return std::get_if<0>(&storage_);
}

const double* StateValue::real() const {
    return std::get_if<1>(&storage_);
}

const bool* StateValue::boolean() const {
    return std::get_if<2>(&storage_);
}

const std::string* StateValue::string() const {
    return std::get_if<3>(&storage_);
}

const Binary* StateValue::binary() const {
    return std::get_if<4>(&storage_);
}

std::int64_t StateValue::toInt64() const {
    switch (kind()) {
        case Kind::Int64:
            return *int64();
        case Kind::Double:
            return saturatingCast<std::int64_t>(*real());
        case Kind::Bool:
            return *boolean() ? 1 : 0;
        case Kind::String:
            return readInteger<std::int64_t, std::uint64_t>(*string());
        case Kind::Binary:
            return 0;
    }
    return 0;
}

int StateValue::toInt() const {
    switch (kind()) {
        case Kind::Int64:
            return static_cast<int>(*int64());
        case Kind::Double:
            return saturatingCast<int>(*real());
        case Kind::Bool:
            return *boolean() ? 1 : 0;
        case Kind::String:
            return readInteger<std::int32_t, std::uint32_t>(*string());
        case Kind::Binary:
            return 0;
    }
    return 0;
}

std::optional<double> StateValue::toDouble() const {
    double result = 0.0;
    switch (kind()) {
        case Kind::Int64:
            result = static_cast<double>(*int64());
            break;
        case Kind::Double:
            result = *real();
            break;
        case Kind::Bool:
            result = *boolean() ? 1.0 : 0.0;
            break;
        case Kind::String:
            result = detail::readLenientDouble(*string());
            break;
        case Kind::Binary:
            result = 0.0;
            break;
    }

    if (!std::isfinite(result))
        return std::nullopt;
    return result;
}

bool StateValue::toBool() const {
    switch (kind()) {
        case Kind::Int64:
            return *int64() != 0;
        case Kind::Double:
            return *real() != 0.0;
        case Kind::Bool:
            return *boolean();
        case Kind::String: {
            const auto& text = *string();
            const auto trimmed = trim(text);
            return readInteger<std::int32_t, std::uint32_t>(text) != 0 ||
                   equalsIgnoreCase(trimmed, "true") || equalsIgnoreCase(trimmed, "yes");
        }
        case Kind::Binary:
            return false;
    }
    return false;
}

std::string StateValue::toString() const {
    switch (kind()) {
        case Kind::Int64:
            return std::to_string(*int64());
        case Kind::Double:
            return detail::writeDouble(*real());
        case Kind::Bool:
            return *boolean() ? "1" : "0";
        case Kind::String:
            return *string();
        case Kind::Binary:
            return encodeBinaryText(*binary());
    }
    return {};
}

const StateValue* StateNode::find(std::string_view key) const {
    for (const auto& property : properties_)
        if (property.key == key)
            return &property.value;
    return nullptr;
}

bool StateNode::set(std::string_view key, StateValue value) {
    if (key.empty())
        return false;

    for (auto& property : properties_) {
        if (property.key == key) {
            property.value = std::move(value);
            return true;
        }
    }

    properties_.push_back({std::string(key), std::move(value)});
    return true;
}

bool StateNode::setInt(std::string_view key, std::int64_t value) {
    return set(key, StateValue::fromInt(value));
}

bool StateNode::setBool(std::string_view key, bool value) {
    return set(key, StateValue::fromBool(value));
}

bool StateNode::setString(std::string_view key, std::string value) {
    return set(key, StateValue::fromString(std::move(value)));
}

bool StateNode::setBinary(std::string_view key, Binary value) {
    return set(key, StateValue::fromBinary(std::move(value)));
}

bool StateNode::setDouble(std::string_view key, double value) {
    auto stored = StateValue::fromDouble(value);
    return stored.has_value() && set(key, std::move(*stored));
}

bool StateNode::remove(std::string_view key) {
    const auto it = std::find_if(properties_.begin(), properties_.end(),
                                 [&](const Property& property) { return property.key == key; });
    if (it == properties_.end())
        return false;
    properties_.erase(it);
    return true;
}

std::int64_t StateNode::getInt64(std::string_view key, std::int64_t fallback) const {
    const auto* value = find(key);
    return value != nullptr ? value->toInt64() : fallback;
}

int StateNode::getInt(std::string_view key, int fallback) const {
    const auto* value = find(key);
    return value != nullptr ? value->toInt() : fallback;
}

double StateNode::getDouble(std::string_view key, double fallback) const {
    const auto* value = find(key);
    if (value == nullptr)
        return fallback;
    return value->toDouble().value_or(fallback);
}

bool StateNode::getBool(std::string_view key, bool fallback) const {
    const auto* value = find(key);
    return value != nullptr ? value->toBool() : fallback;
}

std::string StateNode::getString(std::string_view key, std::string fallback) const {
    const auto* value = find(key);
    return value != nullptr ? value->toString() : std::move(fallback);
}

const Binary* StateNode::getBinary(std::string_view key) const {
    const auto* value = find(key);
    return value != nullptr ? value->binary() : nullptr;
}

StateNode& StateNode::addChild(StateNode child) {
    children_.push_back(std::move(child));
    return children_.back();
}

bool StateNode::removeChild(std::size_t index) {
    if (index >= children_.size())
        return false;
    children_.erase(children_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

const StateNode* StateNode::findChild(std::string_view type) const {
    for (const auto& child : children_)
        if (child.type() == type)
            return &child;
    return nullptr;
}

}  // namespace magda::sdk
