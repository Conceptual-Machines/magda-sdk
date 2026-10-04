#pragma once

#include "magda/sdk/display/DisplayList.hpp"
#include "magda/sdk/meter/MeterModel.hpp"

namespace magda::sdk {

struct MeterLayout {
    enum class Orientation { Vertical, Horizontal };

    float width = 0.0f;
    float height = 0.0f;
    Orientation orientation = Orientation::Vertical;
    /// Vertical only: pins 0 dB to this y and remaps the scale on each side. Negative is off.
    float zeroDbY = -1.0f;
};

/// Meter position of @p db under @p layout, including the zero-dB anchor.
float meterPosition(const MeterScale& scale, const MeterLayout& layout, float db);

/// Replaces @p out with one frame of @p meter: a bar per channel, the peak marks and a 0 dB tick.
void paintMeter(const MeterModel& meter, const MeterLayout& layout, display::DisplayList& out);

}  // namespace magda::sdk
