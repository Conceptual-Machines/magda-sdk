#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "magda/sdk/device/Device.hpp"

using namespace magda::sdk;

namespace {

class RecordingDevice final : public Device {
  public:
    DeviceProperties properties() const override {
        return {};
    }
    void process(ProcessContext&) override {}
    int parameterCount() const override {
        return 1;
    }
    void setParameterValue(int slot, float normalized) override {
        lastSlot = slot;
        lastValue = normalized;
    }

    int lastSlot = -1;
    float lastValue = -1.0f;
};

class VectorOutput final : public MidiOutput {
  public:
    explicit VectorOutput(std::size_t capacity) : capacity_(capacity) {}

    bool addEvent(const MidiEvent& event) override {
        if (events.size() >= capacity_)
            return false;
        events.push_back(event);
        return true;
    }
    void setAllNotesOff(bool value) override {
        panic = value;
    }

    std::vector<MidiEvent> events;
    bool panic = false;

  private:
    std::size_t capacity_;
};

}  // namespace

TEST_CASE("The default segment form applies the first segment through the scalar setter",
          "[device]") {
    RecordingDevice device;
    const std::array<ParameterSegment, 2> segments{{{0, 0.25f, 0.5f}, {64, 0.5f, 0.75f}}};
    device.setParameterSegments(3, segments);
    CHECK(device.lastSlot == 3);
    CHECK(device.lastValue == 0.25f);

    device.lastSlot = -1;
    device.setParameterSegments(3, {});
    CHECK(device.lastSlot == -1);
}

TEST_CASE("A restore result is ok or carries a message", "[device]") {
    CHECK(RestoreResult::success().ok);
    const auto failed = RestoreResult::error("bad path");
    CHECK_FALSE(failed.ok);
    CHECK(failed.message == "bad path");
}

TEST_CASE("A short MIDI event holds its bytes inline", "[midi]") {
    const auto on = MidiEvent::noteOn(1, 60, 100, 12, 0.5f, 7);
    CHECK(on.isNoteOn());
    CHECK_FALSE(on.isNoteOff());
    CHECK_FALSE(on.isSysex());
    CHECK(on.channel() == 1);
    CHECK(on.noteNumber() == 60);
    CHECK(on.velocity() == 100);
    CHECK(on.sample == 12);
    CHECK(on.fraction == 0.5f);
    CHECK(on.sourceId == 7);
    CHECK(on.size() == 3);

    CHECK(MidiEvent::noteOn(16, 60, 0, 0).isNoteOff());
    CHECK(MidiEvent::noteOn(16, 60, 0, 0).channel() == 16);
    CHECK(MidiEvent::noteOff(2, 61, 64, 0).isNoteOff());
    CHECK(MidiEvent::controller(3, 7, 99, 0).isController());
}

TEST_CASE("A long MIDI event points at the caller's bytes", "[midi]") {
    const std::array<std::uint8_t, 5> sysex{0xF0, 1, 2, 3, 0xF7};
    const auto event = MidiEvent::fromBytes(sysex.data(), sysex.size(), 4);
    CHECK(event.isSysex());
    CHECK(event.data() == sysex.data());
    CHECK(event.size() == 5);
    CHECK_FALSE(event.isNoteOn());
    CHECK(event.channel() == 0);

    const std::array<std::uint8_t, 2> program{0xC1, 5};
    const auto shortEvent = MidiEvent::fromBytes(program.data(), 2, 0);
    CHECK_FALSE(shortEvent.isSysex());
    CHECK(shortEvent.size() == 2);
}

TEST_CASE("An output sink refuses once it is full", "[midi]") {
    VectorOutput out(2);
    CHECK(out.addEvent(MidiEvent::noteOn(1, 60, 100, 0)));
    CHECK(out.addEvent(MidiEvent::noteOn(1, 61, 100, 0)));
    CHECK_FALSE(out.addEvent(MidiEvent::noteOn(1, 62, 100, 0)));
    CHECK(out.events.size() == 2);
}

TEST_CASE("An event position floors with a hundredth of a sample of slack", "[midi]") {
    const auto exact = midiEventPosition(10.0 / 48000.0, 48000.0);
    CHECK(exact.sample == 10);
    CHECK(exact.fraction < 0.01f);

    const auto nearlyOnto = midiEventPosition(9.995 / 48000.0, 48000.0);
    CHECK(nearlyOnto.sample == 10);
    CHECK(nearlyOnto.fraction == 0.0f);

    const auto between = midiEventPosition(10.6 / 48000.0, 48000.0);
    CHECK(between.sample == 10);
    CHECK(between.fraction > 0.59f);
    CHECK(between.fraction < 0.61f);
}

TEST_CASE("A process context reports its block length from the audio view", "[device]") {
    std::array<float, 8> left{};
    std::array<float*, 1> channels{left.data()};
    ProcessContext context;
    context.audio = magda::BufferView(channels.data(), 1, 8);
    CHECK(context.numSamples() == 8);
    CHECK_FALSE(context.sidechain.has_value());
    CHECK(context.liveSourceIds.empty());
}
