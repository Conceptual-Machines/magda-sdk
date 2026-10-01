#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "magda/sdk/state/StateNode.hpp"

namespace magda::sdk {

/// The schema version this build reads and writes.
inline constexpr int kStateSchemaVersion = 2;

/// The oldest schema the canonical decoder reads.
inline constexpr int kMinStateSchemaVersion = 2;

/// Deepest node nesting the codec reads or writes, the root being depth 1.
inline constexpr int kMaxStateDepth = 64;

/**
 * @brief A device's saved state: the schema it was written under, the device it belongs to, and
 *        everything the device owns that is not an automatable parameter.
 *
 * The root has an empty type.
 */
struct StateDocument {
    int schema = kStateSchemaVersion;
    std::string deviceType;
    StateNode root;

    bool operator==(const StateDocument&) const = default;
};

enum class DecodeStatus {
    Ok,
    /// Not parseable JSON.
    NotJson,
    /// JSON, but without the `schema` and `device` members of a document.
    NotADocument,
    /// A schema older than kMinStateSchemaVersion.
    UnsupportedSchema,
    /// A schema newer than kStateSchemaVersion. Never rewrite the source (see isFutureSchema).
    FutureSchema,
    /// A schema this build reads, with content the canonical form does not allow.
    Invalid,
};

struct DecodeResult {
    DecodeStatus status = DecodeStatus::NotJson;
    std::string message;
    std::optional<StateDocument> document;

    bool ok() const {
        return status == DecodeStatus::Ok;
    }
};

/// Parse the canonical JSON form (docs/device-state.md). Strict: it reads nothing it would not
/// write.
DecodeResult decodeDocument(std::string_view json);

/**
 * @brief The canonical JSON text of @p document.
 *
 * Refuses, returning nullopt and saying why in @p error, a document it could not decode again:
 * a schema other than kStateSchemaVersion, an empty device type or property key, an empty child
 * type, a typed root, invalid UTF-8, or nesting past kMaxStateDepth.
 */
std::optional<std::string> encodeDocument(const StateDocument& document,
                                          std::string* error = nullptr);

/// The `schema` of a JSON object that has `schema` and `device`; nullopt for anything else.
std::optional<int> schemaVersionOf(std::string_view json);

/**
 * @brief Whether @p json is a document from a schema newer than this build reads.
 *
 * decodeDocument refuses such a document, so a device loads its defaults; a writer must then leave
 * the saved text alone, or opening and saving would silently downgrade it.
 */
bool isFutureSchema(std::string_view json);

}  // namespace magda::sdk
