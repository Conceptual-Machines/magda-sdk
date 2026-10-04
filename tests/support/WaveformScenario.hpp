#pragma once

#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "magda/sdk/audio/BufferView.hpp"
#include "magda/sdk/display/DisplayList.hpp"
#include "magda/sdk/peaks/PeakData.hpp"
#include "magda/sdk/state/detail/Json.hpp"
#include "magda/sdk/waveview/WaveformView.hpp"

/// Runs tests/golden/waveform-view/scenarios.json, natively and in the Canvas demo's wasm.
namespace magda::sdk::waveform_scenario {

inline double numberOf(const detail::JsonValue* value, double fallback) {
    if (value == nullptr)
        return fallback;
    if (value->type == detail::JsonValue::Type::Int)
        return static_cast<double>(value->integer);
    if (value->type == detail::JsonValue::Type::Double)
        return value->real;
    return fallback;
}

inline bool flagOf(const detail::JsonValue* value) {
    return value != nullptr && value->type == detail::JsonValue::Type::Bool && value->boolean;
}

/// A decaying, pulsing tone: loud at the start, with a slow swell the zoom can find.
inline std::vector<float> burst(double seconds, double sampleRate) {
    const auto count = static_cast<std::size_t>(seconds * sampleRate);
    std::vector<float> samples(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double envelope =
            std::exp(-1.5 * t) * (0.2 + 0.8 * std::abs(std::sin(1.5 * std::numbers::pi * t)));
        samples[i] = static_cast<float>(envelope * std::sin(2.0 * std::numbers::pi * 110.0 * t));
    }
    return samples;
}

/// What a case's view reads from, kept alive beside it.
struct Source {
    std::vector<float> samples;
    double sampleRate = 8000.0;
    std::unique_ptr<PeakData> peaks;
    std::unique_ptr<WaveformSource> view;

    double seconds() const {
        return static_cast<double>(samples.size()) / sampleRate;
    }
};

/// Builds the source a case names; through PeakData when it sets "peaks".
inline std::unique_ptr<Source> sourceOf(const detail::JsonValue& c) {
    auto source = std::make_unique<Source>();
    const auto* spec = c.member("source");
    source->sampleRate = numberOf(spec ? spec->member("sampleRate") : nullptr, 8000.0);
    source->samples =
        burst(numberOf(spec ? spec->member("seconds") : nullptr, 2.0), source->sampleRate);
    const auto numSamples = static_cast<std::int64_t>(source->samples.size());
    if (spec != nullptr && flagOf(spec->member("peaks"))) {
        source->peaks = std::make_unique<PeakData>(1, numSamples);
        const float* channels[] = {source->samples.data()};
        source->peaks->addBlock(ConstBufferView(channels, 1, static_cast<int>(numSamples)), 0);
        source->view = std::make_unique<PeakDataWaveformSource>(*source->peaks, 0);
    } else {
        source->view = std::make_unique<BufferWaveformSource>(source->samples.data(), numSamples);
    }
    return source;
}

/// Configures @p view from a case; @p source must outlive it.
inline void configure(const detail::JsonValue& c, const Source& source, WaveformView& view) {
    const auto* size = c.member("size");
    view.setSize(static_cast<int>(numberOf(size ? &size->array.at(0) : nullptr, 300)),
                 static_cast<int>(numberOf(size ? &size->array.at(1) : nullptr, 100)));
    view.setSource(source.view.get(), source.seconds());
    view.setGain(static_cast<float>(numberOf(c.member("gain"), 1.0)));
    WaveformMarkers markers;
    markers.end = source.seconds();
    if (const auto* m = c.member("markers")) {
        markers.start = numberOf(m->member("start"), 0.0);
        markers.end = numberOf(m->member("end"), markers.end);
        markers.loop = flagOf(m->member("loop"));
        markers.loopStart = numberOf(m->member("loopStart"), 0.0);
        markers.loopEnd = numberOf(m->member("loopEnd"), 0.0);
    }
    view.setMarkers(markers);
    view.setPlayhead(numberOf(c.member("playhead"), 0.0));
}

inline void play(const detail::JsonValue& e, WaveformView& view) {
    const auto type = e.member("type") != nullptr ? e.member("type")->string : std::string{};
    WaveformPointer pointer;
    pointer.x = static_cast<float>(numberOf(e.member("x"), 0.0));
    pointer.y = static_cast<float>(numberOf(e.member("y"), 0.0));
    if (const auto* mods = e.member("mods"))
        for (const auto& m : mods->array) {
            pointer.shift |= m.string == "shift";
            pointer.command |= m.string == "command";
            pointer.alt |= m.string == "alt";
            pointer.middle |= m.string == "middle";
        }
    if (type == "down")
        view.pointerDown(pointer);
    else if (type == "drag")
        view.pointerDrag(pointer);
    else if (type == "up")
        view.pointerUp(pointer);
    else if (type == "zoom")
        view.zoomBy(numberOf(e.member("factor"), 1.0), pointer.x);
}

/// Configures a view from case @p c, plays its events and renders the final frame.
inline display::DisplayList run(const detail::JsonValue& c) {
    const auto source = sourceOf(c);
    WaveformView view;
    configure(c, *source, view);
    if (const auto* events = c.member("events"))
        for (const auto& e : events->array)
            play(e, view);
    display::DisplayList list;
    view.render(list);
    return list;
}

}  // namespace magda::sdk::waveform_scenario
