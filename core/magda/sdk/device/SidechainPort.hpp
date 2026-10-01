#pragma once

namespace magda::sdk {

/// What a device declares on its sidechain slot. Declared, never inferred from channel counts.
struct SidechainPort {
    enum class Kind {
        None,
        /// Audio on the device's own port, not further channels of the buffer it processes.
        Audio,
        /// MIDI from another track, which reaches the device on its MIDI input.
        MIDI,
    };

    Kind kind = Kind::None;
    /// Channels an audio key carries. Zero for every other kind.
    int channels = 0;

    bool takesAudio() const {
        return kind == Kind::Audio && channels > 0;
    }
    bool declared() const {
        return kind != Kind::None;
    }

    bool operator==(const SidechainPort&) const = default;
};

/// The audio key most dynamics devices ask for.
inline constexpr SidechainPort monoAudioSidechain{.kind = SidechainPort::Kind::Audio,
                                                  .channels = 1};

}  // namespace magda::sdk
