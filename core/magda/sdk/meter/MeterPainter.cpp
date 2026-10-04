#include "magda/sdk/meter/MeterPainter.hpp"

#include <algorithm>

namespace magda::sdk {

namespace {

using display::Colour;
using display::ColourRole;
using display::DisplayList;
using display::LinearGradient;
using display::Rect;
using Orientation = MeterLayout::Orientation;

constexpr float kBarGap = 1.0f;
constexpr float kBarRadius = 1.0f;
constexpr float kPeakThickness = 1.5f;
constexpr float kPeakAlpha = 0.9f;
constexpr float kTickAlpha = 0.5f;
constexpr float kMidDb = -12.0f;
constexpr float kGradientFade = 0.03f;

ColourRole roleForDb(float db) {
    return db >= 0.0f     ? ColourRole::MeterHigh
           : db >= kMidDb ? ColourRole::MeterMid
                          : ColourRole::MeterLow;
}

/// Low to mid to high, each held solid with a short fade around -12 dB and 0 dB.
LinearGradient meterGradient(const MeterScale& scale, const MeterLayout& layout, Rect bounds) {
    const float midPos = meterPosition(scale, layout, kMidDb);
    const float highPos = meterPosition(scale, layout, 0.0f);
    const auto low = Colour::of(ColourRole::MeterLow);
    const auto mid = Colour::of(ColourRole::MeterMid);
    const auto high = Colour::of(ColourRole::MeterHigh);

    LinearGradient gradient;
    if (layout.orientation == Orientation::Horizontal) {
        gradient.from = {bounds.x, 0.0f};
        gradient.to = {bounds.right(), 0.0f};
    } else {
        gradient.from = {0.0f, bounds.bottom()};
        gradient.to = {0.0f, bounds.y};
    }
    gradient.stops = {
        {0.0f, low},
        {std::max(0.0f, midPos - kGradientFade), low},
        {std::min(1.0f, midPos + kGradientFade), mid},
        {std::max(0.0f, highPos - kGradientFade), mid},
        {std::min(1.0f, highPos + kGradientFade), high},
        {1.0f, high},
    };
    std::stable_sort(gradient.stops.begin(), gradient.stops.end(),
                     [](const auto& a, const auto& b) { return a.position < b.position; });
    return gradient;
}

void paintBar(const MeterModel& meter, const MeterLayout& layout, Rect bounds,
              const MeterModel::Channel& channel, DisplayList& out) {
    const auto& scale = meter.scale();
    out.fillRect(bounds, Colour::of(ColourRole::Surface), kBarRadius);

    const float level = meterPosition(scale, layout, scale.gainToDb(channel.displayGain));
    const float peakPos = meterPosition(scale, layout, channel.peakDb);
    const auto peakColour = Colour::of(roleForDb(channel.peakDb)).withAlpha(kPeakAlpha);

    if (layout.orientation == Orientation::Horizontal) {
        const float fillWidth = bounds.width * level;
        if (fillWidth >= 1.0f)
            out.fillRect({bounds.x, bounds.y, fillWidth, bounds.height},
                         meterGradient(scale, layout, bounds), kBarRadius);
        if (peakPos > 0.01f)
            out.fillRect(
                {bounds.x + bounds.width * peakPos, bounds.y, kPeakThickness, bounds.height},
                peakColour);
        return;
    }

    const float fillHeight = bounds.height * level;
    if (fillHeight >= 1.0f)
        out.fillRect({bounds.x, bounds.bottom() - fillHeight, bounds.width, fillHeight},
                     meterGradient(scale, layout, bounds), kBarRadius);
    if (peakPos > 0.01f)
        out.fillRect(
            {bounds.x, bounds.bottom() - bounds.height * peakPos, bounds.width, kPeakThickness},
            peakColour);
}

}  // namespace

float meterPosition(const MeterScale& scale, const MeterLayout& layout, float db) {
    const float base = scale.dbToPosition(db);
    if (layout.orientation != Orientation::Vertical || layout.zeroDbY < 0.0f ||
        layout.height <= 0.0f)
        return base;

    const float baseZero = scale.dbToPosition(0.0f);
    const float anchoredZero = std::clamp(1.0f - layout.zeroDbY / layout.height, 0.0f, 1.0f);

    if (base <= baseZero)
        return baseZero > 0.0f ? base * anchoredZero / baseZero : 0.0f;

    const float upperRange = 1.0f - baseZero;
    return upperRange > 0.0f ? anchoredZero + (base - baseZero) * (1.0f - anchoredZero) / upperRange
                             : 1.0f;
}

void paintMeter(const MeterModel& meter, const MeterLayout& layout, DisplayList& out) {
    out.reset(layout.width, layout.height);
    const Rect bounds{0.0f, 0.0f, layout.width, layout.height};
    // Opaque, so a container fill does not show through the bar gap and the rounded corners.
    out.fillRect(bounds, Colour::of(ColourRole::Background));

    const int numBars = meter.numChannels();
    const float zeroPos = meterPosition(meter.scale(), layout, 0.0f);
    const auto tickColour = Colour::of(ColourRole::Border).withAlpha(kTickAlpha);

    if (layout.orientation == Orientation::Horizontal) {
        const float barHeight = (layout.height - kBarGap * static_cast<float>(numBars - 1)) /
                                static_cast<float>(numBars);
        for (int i = 0; i < numBars; ++i)
            paintBar(meter, layout,
                     {0.0f, static_cast<float>(i) * (barHeight + kBarGap), layout.width, barHeight},
                     meter.channel(i), out);

        // Whole pixels, as juce::Graphics::drawVerticalLine draws.
        const auto tickX = static_cast<float>(static_cast<int>(layout.width * zeroPos));
        out.fillRect({tickX, 0.0f, 1.0f, layout.height}, tickColour);
        return;
    }

    const float barWidth =
        (layout.width - kBarGap * static_cast<float>(numBars - 1)) / static_cast<float>(numBars);
    for (int i = 0; i < numBars; ++i)
        paintBar(meter, layout,
                 {static_cast<float>(i) * (barWidth + kBarGap), 0.0f, barWidth, layout.height},
                 meter.channel(i), out);

    const auto tickY =
        static_cast<float>(static_cast<int>(layout.height - layout.height * zeroPos));
    out.fillRect({0.0f, tickY, layout.width, 1.0f}, tickColour);
}

}  // namespace magda::sdk
