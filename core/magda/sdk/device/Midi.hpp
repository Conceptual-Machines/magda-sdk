#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace magda::sdk {

/**
 * @brief One MIDI event: a message and where in the block it falls.
 *
 * A short message (up to three bytes) is held inline. A longer one, which is sysex, points at
 * bytes the owner of the event keeps alive: for input that is the host's block storage, valid for
 * the process call. A sink copies a long message into its own storage when it accepts it.
 */
struct MidiEvent {
    std::array<std::uint8_t, 3> bytes{};
    /// Bytes in use in @ref bytes; zero when the message is long.
    std::uint8_t numBytes = 0;
    const std::uint8_t* longData = nullptr;
    std::uint32_t longSize = 0;

    /// The sample, from the start of the block, the event falls in.
    std::int32_t sample = 0;

    /// How far into @ref sample the event falls, in [0, 1).
    float fraction = 0.0f;

    /// Which source produced it, against ProcessContext::liveSourceIds.
    std::uint32_t sourceId = 0;

    /// A message of up to three bytes, copied in; longer ones keep a pointer into @p data.
    static MidiEvent fromBytes(const std::uint8_t* data, std::uint32_t size, std::int32_t sample,
                               float fraction = 0.0f, std::uint32_t sourceId = 0) {
        MidiEvent event;
        event.sample = sample;
        event.fraction = fraction;
        event.sourceId = sourceId;
        if (size <= 3) {
            for (std::uint32_t i = 0; i < size; ++i)
                event.bytes[i] = data[i];
            event.numBytes = static_cast<std::uint8_t>(size);
        } else {
            event.longData = data;
            event.longSize = size;
        }
        return event;
    }

    /// @p channel is 1 to 16.
    static MidiEvent noteOn(int channel, int note, int velocity, std::int32_t sample,
                            float fraction = 0.0f, std::uint32_t sourceId = 0) {
        return channelMessage(0x90, channel, note, velocity, sample, fraction, sourceId);
    }

    static MidiEvent noteOff(int channel, int note, int velocity, std::int32_t sample,
                             float fraction = 0.0f, std::uint32_t sourceId = 0) {
        return channelMessage(0x80, channel, note, velocity, sample, fraction, sourceId);
    }

    static MidiEvent controller(int channel, int number, int value, std::int32_t sample,
                                float fraction = 0.0f, std::uint32_t sourceId = 0) {
        return channelMessage(0xB0, channel, number, value, sample, fraction, sourceId);
    }

    bool isSysex() const {
        return longData != nullptr;
    }

    const std::uint8_t* data() const {
        return isSysex() ? longData : bytes.data();
    }

    std::uint32_t size() const {
        return isSysex() ? longSize : numBytes;
    }

    std::uint8_t status() const {
        return size() > 0 ? data()[0] : std::uint8_t{0};
    }

    bool isChannelMessage() const {
        return !isSysex() && status() >= 0x80 && status() < 0xF0;
    }

    /// Channel 1 to 16; zero for a message that has none.
    int channel() const {
        return isChannelMessage() ? (status() & 0x0F) + 1 : 0;
    }

    /// A note-on with a velocity above zero.
    bool isNoteOn() const {
        return isChannelMessage() && (status() & 0xF0) == 0x90 && numBytes == 3 && bytes[2] > 0;
    }

    /// A note-off, or a note-on with velocity zero.
    bool isNoteOff() const {
        return isChannelMessage() && numBytes == 3 &&
               ((status() & 0xF0) == 0x80 || ((status() & 0xF0) == 0x90 && bytes[2] == 0));
    }

    bool isController() const {
        return isChannelMessage() && (status() & 0xF0) == 0xB0 && numBytes == 3;
    }

    /// First data byte: the note number, controller number or program.
    int data1() const {
        return numBytes > 1 ? bytes[1] : 0;
    }

    /// Second data byte: the velocity or controller value.
    int data2() const {
        return numBytes > 2 ? bytes[2] : 0;
    }

    int noteNumber() const {
        return data1();
    }

    int velocity() const {
        return data2();
    }

  private:
    static MidiEvent channelMessage(int status, int channel, int data1, int data2,
                                    std::int32_t sample, float fraction, std::uint32_t sourceId) {
        const std::uint8_t bytes[3] = {
            static_cast<std::uint8_t>(status | ((channel - 1) & 0x0F)),
            static_cast<std::uint8_t>(data1 & 0x7F),
            static_cast<std::uint8_t>(data2 & 0x7F),
        };
        return fromBytes(bytes, 3, sample, fraction, sourceId);
    }
};

/**
 * @brief The MIDI that reached the device this block, in block order. Audio thread.
 *
 * Read-only: whether the stream continues past the device is the host's routing decision
 * (thru), never the device's.
 */
class MidiInput {
  public:
    virtual ~MidiInput() = default;

    virtual int size() const = 0;
    virtual const MidiEvent& event(int index) const = 0;

    /// The host signalled panic without a controller event (a playhead jump, a stop).
    virtual bool isAllNotesOff() const = 0;
};

/**
 * @brief Where a device writes the MIDI it emits. Audio thread.
 *
 * Empty on entry; on exit it is the device's whole output. Capacity is bounded per port, in
 * events and in bytes, and never grows.
 */
class MidiOutput {
  public:
    virtual ~MidiOutput() = default;

    /**
     * @brief Append @p event; false, appending nothing, when the port's event count or byte
     *        budget is exhausted. Sysex bytes are copied.
     */
    virtual bool addEvent(const MidiEvent& event) = 0;

    /// Panic beside the events, for a host whose MIDI container carries one.
    virtual void setAllNotesOff(bool allNotesOff) = 0;
};

/// Where a time falls: the sample and how far into it, in [0, 1).
struct MidiEventPosition {
    std::int32_t sample = 0;
    float fraction = 0.0f;
};

/**
 * @brief The sample a time @p seconds into the block falls in at @p sampleRate, and how far into
 * it.
 *
 * Floor with a hundredth of a sample of slack, so a time that lands 0.001 short of a sample
 * boundary plays on that sample.
 */
inline MidiEventPosition midiEventPosition(double seconds, double sampleRate) {
    const auto position = seconds * sampleRate;
    const auto sample = std::floor(position + 0.01);
    const auto fraction = position - sample;
    return {static_cast<std::int32_t>(sample), static_cast<float>(fraction > 0.0 ? fraction : 0.0)};
}

}  // namespace magda::sdk
