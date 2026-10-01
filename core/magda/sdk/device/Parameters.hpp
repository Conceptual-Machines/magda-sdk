#pragma once

namespace magda::sdk {

/**
 * @brief A parameter's value over part of a block, normalized to [0, 1].
 *
 * Linear from @ref startValue at @ref startSample to @ref endValue where the next segment
 * starts, or at the end of the block for the last one.
 */
struct ParameterSegment {
    int startSample = 0;
    float startValue = 0.0f;
    float endValue = 0.0f;
};

}  // namespace magda::sdk
