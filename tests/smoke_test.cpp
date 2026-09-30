#include <cstdlib>

#include "magda/sdk/version.hpp"

int main() {
    const auto v = magda::sdk::version();
    const bool ok = v.major >= 0 && !magda::sdk::versionString().empty();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
