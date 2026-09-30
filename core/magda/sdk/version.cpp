#include "magda/sdk/version.hpp"

#include "magda/sdk/version_config.hpp"

namespace magda::sdk {

Version version() noexcept {
    return {MAGDA_SDK_VERSION_MAJOR, MAGDA_SDK_VERSION_MINOR, MAGDA_SDK_VERSION_PATCH};
}

std::string_view versionString() noexcept {
    return MAGDA_SDK_VERSION_STRING;
}

}  // namespace magda::sdk
