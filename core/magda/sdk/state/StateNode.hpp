#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace magda::sdk {

using Binary = std::vector<std::uint8_t>;

/**
 * @brief One property value: int64, double, bool, string or binary.
 *
 * Doubles are finite by construction. The coercing readers follow juce::var, so a value written
 * as one kind reads as another without failing (docs/device-state.md).
 */
class StateValue {
  public:
    enum class Kind { Int64, Double, Bool, String, Binary };

    static StateValue fromInt(std::int64_t value);
    static StateValue fromBool(bool value);
    static StateValue fromString(std::string value);
    static StateValue fromBinary(Binary value);

    /// Nullopt when @p value is not finite.
    static std::optional<StateValue> fromDouble(double value);

    Kind kind() const;

    /// The stored value when it is exactly this kind, otherwise null.
    const std::int64_t* int64() const;
    const double* real() const;
    const bool* boolean() const;
    const std::string* string() const;
    const Binary* binary() const;

    std::int64_t toInt64() const;

    /// Wraps like a C cast from int64, so out-of-range values truncate.
    int toInt() const;

    /// Nullopt when the coercion is not finite (a string such as "nan" or "1e999").
    std::optional<double> toDouble() const;
    bool toBool() const;
    std::string toString() const;

    bool operator==(const StateValue&) const = default;

  private:
    using Storage = std::variant<std::int64_t, double, bool, std::string, Binary>;

    explicit StateValue(Storage storage) : storage_(std::move(storage)) {}

    Storage storage_;
};

/**
 * @brief A named node: ordered unique-key properties and ordered children.
 *
 * Insertion order survives a round trip. A property key names one value; setting an existing key
 * replaces it in place.
 */
class StateNode {
  public:
    struct Property {
        std::string key;
        StateValue value;

        bool operator==(const Property&) const = default;
    };

    StateNode() = default;
    explicit StateNode(std::string type) : type_(std::move(type)) {}

    const std::string& type() const {
        return type_;
    }
    void setType(std::string type) {
        type_ = std::move(type);
    }

    std::span<const Property> properties() const {
        return properties_;
    }

    const StateValue* find(std::string_view key) const;
    bool has(std::string_view key) const {
        return find(key) != nullptr;
    }

    /// Replaces in place, or appends. False, storing nothing, for an empty key.
    bool set(std::string_view key, StateValue value);

    bool setInt(std::string_view key, std::int64_t value);
    bool setBool(std::string_view key, bool value);
    bool setString(std::string_view key, std::string value);
    bool setBinary(std::string_view key, Binary value);

    /// False, storing nothing, for a non-finite @p value or an empty key.
    bool setDouble(std::string_view key, double value);

    bool remove(std::string_view key);

    /// Typed reads: @p fallback only when the key is absent, never for a kind mismatch.
    std::int64_t getInt64(std::string_view key, std::int64_t fallback = 0) const;
    int getInt(std::string_view key, int fallback = 0) const;
    double getDouble(std::string_view key, double fallback = 0.0) const;
    bool getBool(std::string_view key, bool fallback = false) const;
    std::string getString(std::string_view key, std::string fallback = {}) const;

    /// Binary is never coerced: null unless the value is binary.
    const Binary* getBinary(std::string_view key) const;

    std::span<const StateNode> children() const {
        return children_;
    }
    std::span<StateNode> children() {
        return children_;
    }

    StateNode& addChild(StateNode child);
    bool removeChild(std::size_t index);

    /// First child of @p type, or null.
    const StateNode* findChild(std::string_view type) const;

    bool isEmpty() const {
        return properties_.empty() && children_.empty();
    }

    bool operator==(const StateNode&) const = default;

  private:
    std::string type_;
    std::vector<Property> properties_;
    std::vector<StateNode> children_;
};

}  // namespace magda::sdk
