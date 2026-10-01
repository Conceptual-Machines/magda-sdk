#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "magda/sdk/audio/BufferView.hpp"
#include "magda/sdk/device/Midi.hpp"
#include "magda/sdk/device/TempoMap.hpp"

namespace magda::sdk {

/// Everything a device is handed for one block. Valid for the process call only.
struct ProcessContext {
    /// The device's input on entry and its output on exit: devices process in place.
    BufferView audio;

    /**
     * @brief The sidechain key, read-only, on its own port.
     *
     * Absent when nothing is routed, which is "no key" and not a silent one. Fewer channels than
     * the device declared is legal, so a device reads what is there.
     */
    std::optional<ConstBufferView> sidechain;

    /// Both null when the host routed no MIDI to or from the device, otherwise both set.
    const MidiInput* midiIn = nullptr;
    MidiOutput* midiOut = nullptr;

    const TempoMap* tempoMap = nullptr;

    double timelineStartSeconds = 0.0;
    double timelineEndSeconds = 0.0;
    bool isPlaying = false;

    /// An offline render: skip live-only work such as analysis taps and file reads.
    bool isRendering = false;

    /**
     * @brief Sources the host counts as live input, against MidiEvent::sourceId.
     *
     * Empty when the host does not say, which reads as "no source is known live", never as "all
     * of them".
     */
    std::span<const std::uint32_t> liveSourceIds;

    int numSamples() const {
        return audio.numFrames();
    }
};

}  // namespace magda::sdk
