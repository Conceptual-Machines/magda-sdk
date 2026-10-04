#pragma once

#include <juce_graphics/juce_graphics.h>

#include <functional>

#include "magda/sdk/display/DisplayList.hpp"

namespace magda::sdk::juce_host {

/// The shell's theme: the colour each role draws in. Read at paint time.
using ColourResolver = std::function<juce::Colour(display::ColourRole)>;

/// The shell's font for text commands of a given size.
using FontResolver = std::function<juce::Font(float fontSize)>;

/// The SDK reference palette (display::defaultColour).
juce::Colour defaultColour(display::ColourRole role);

/// Draws @p list at @p g's origin, one display unit per JUCE logical pixel. Without a
/// @p font, text uses JUCE's default typeface.
void drawDisplayList(juce::Graphics& g, const display::DisplayList& list,
                     const ColourResolver& resolve = defaultColour, const FontResolver& font = {});

}  // namespace magda::sdk::juce_host
