#pragma once

#include <juce_graphics/juce_graphics.h>

#include <functional>

#include "magda/sdk/display/DisplayList.hpp"

namespace magda::sdk::juce_host {

/// The shell's theme: the colour each role draws in. Read at paint time.
using ColourResolver = std::function<juce::Colour(display::ColourRole)>;

/// The SDK reference palette (display::defaultColour).
juce::Colour defaultColour(display::ColourRole role);

/// Draws @p list at @p g's origin, one display unit per JUCE logical pixel.
void drawDisplayList(juce::Graphics& g, const display::DisplayList& list,
                     const ColourResolver& resolve = defaultColour);

}  // namespace magda::sdk::juce_host
