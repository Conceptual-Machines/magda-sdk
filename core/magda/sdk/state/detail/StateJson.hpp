#pragma once

#include "magda/sdk/state/StateCodec.hpp"
#include "magda/sdk/state/detail/Json.hpp"

namespace magda::sdk::detail {

/// decodeDocument over an already parsed value, for a document embedded in another one.
DecodeResult decodeDocumentValue(const JsonValue& parsed);

}  // namespace magda::sdk::detail
