#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "magda/sdk/abi/AbiDevice.hpp"
#include "magda/sdk/abi/AbiHarness.hpp"

using magda::sdk::host::AbiDevice;
using magda::sdk::host::linkedModule;

namespace {

const magda_device_api& api() {
    return *linkedModule()->api;
}

struct Stereo {
    explicit Stereo(int frames)
        : left(static_cast<std::size_t>(frames)),
          right(left.size()),
          pointers{left.data(), right.data()} {}
    std::vector<float> left, right;
    std::array<float*, 2> pointers;
};

magda_process processOf(Stereo& audio, int frames) {
    magda_process p{};
    p.struct_tag = MAGDA_TAG_PROCESS;
    p.struct_size = sizeof(p);
    p.num_frames = frames;
    p.num_channels = 2;
    p.channels = audio.pointers.data();
    return p;
}

std::string typeManifest(std::string_view id) {
    const auto* module = linkedModule();
    for (int i = 0; i < module->device_type_count; ++i)
        if (id == module->device_types[i].id)
            return module->device_types[i].manifest;
    return {};
}

}  // namespace

TEST_CASE("The reference module conforms", "[abi]") {
    const auto failures = magda::sdk::host::checkModuleConformance(linkedModule());
    for (const auto& failure : failures)
        UNSCOPED_INFO(failure);
    CHECK(failures.empty());
}

TEST_CASE("The entry table carries the versions and the device types in order", "[abi]") {
    const auto* module = linkedModule();
    REQUIRE(module != nullptr);
    CHECK(magda_module_entry(0) == nullptr);
    CHECK(module->abi_version == MAGDA_ABI_VERSION);
    CHECK(module->state_schema == 2);
    REQUIRE(module->device_type_count == 3);
    CHECK(std::string_view(module->device_types[0].id) == "sdkReferenceSine");
    CHECK(std::string_view(module->device_types[0].name) == "Reference Sine");
    CHECK(typeManifest("sdkReferenceSine").find("\"id\":\"frequency\"") != std::string::npos);
}

TEST_CASE("A call longer than the prepared block renders the same as block-sized calls", "[abi]") {
    AbiDevice whole(*linkedModule(), "sdkReferenceSine");
    AbiDevice blocks(*linkedModule(), "sdkReferenceSine");
    REQUIRE(whole.prepare(48000.0, 64) == MAGDA_OK);
    REQUIRE(blocks.prepare(48000.0, 64) == MAGDA_OK);

    const std::uint8_t noteOn[] = {0x90, 81, 100};
    Stereo a(200), b(200);
    whole.queueMidi(noteOn, 3, 150);
    REQUIRE(whole.process(a.pointers.data(), 2, 200) == MAGDA_OK);

    for (int start = 0; start < 200; start += 64) {
        const int count = std::min(64, 200 - start);
        if (start <= 150 && 150 < start + count)
            blocks.queueMidi(noteOn, 3, 150 - start);
        std::array<float*, 2> pointers{b.left.data() + start, b.right.data() + start};
        blocks.process(pointers.data(), 2, count);
    }
    CHECK(a.left == b.left);
    CHECK(a.right == a.left);
}

TEST_CASE("Process takes at most the prepared frames and MIDI in block order", "[abi]") {
    AbiDevice sine(*linkedModule(), "sdkReferenceSine");
    REQUIRE(sine.prepare(48000.0, 32) == MAGDA_OK);
    Stereo audio(64);
    auto p = processOf(audio, 33);
    CHECK(api().process(sine.get(), &p) == MAGDA_ERR_ARGUMENT);

    std::array<magda_midi_event, 2> events{};
    events[0].sample = 10;
    events[0].size = 3;
    events[1].sample = 4;
    events[1].size = 3;
    std::array<magda_midi_event, 4> outEvents{};
    magda_midi_in in{events.data(), 2, 0};
    magda_midi_out out{outEvents.data(), 4, 0, nullptr, 0, 0, 0, 0};
    p.num_frames = 32;
    p.midi_in = &in;
    p.midi_out = &out;
    CHECK(api().process(sine.get(), &p) == MAGDA_ERR_ARGUMENT);
    std::swap(events[0], events[1]);
    CHECK(api().process(sine.get(), &p) == MAGDA_OK);
}

TEST_CASE("Parameters are normalized and convert through the manifest's reference", "[abi]") {
    AbiDevice sine(*linkedModule(), "sdkReferenceSine");
    auto* handle = sine.get();
    REQUIRE(api().param_count(handle) == 2);
    CHECK(api().param_to_real(handle, 0, api().param_value(handle, 0)) ==
          Catch::Approx(440.0f).epsilon(1e-4));
    CHECK(api().set_param(handle, 1, 2.0f) == MAGDA_OK);
    CHECK(api().param_value(handle, 1) == 1.0f);
    CHECK(api().set_param(handle, 5, 0.5f) == MAGDA_ERR_ARGUMENT);
    CHECK(api().param_to_normalized(handle, 1, -30.0f) == Catch::Approx(0.5f));
    CHECK(api().param_offered(handle, 0) == 1);
    CHECK(api().param_offered(handle, 9) == 0);

    std::array<char, 512> text{};
    std::int32_t size = 0;
    REQUIRE(api().param_descriptor(handle, 0, text.data(), text.size(), &size) == MAGDA_OK);
    CHECK(std::string_view(text.data(), static_cast<std::size_t>(size))
              .starts_with("{\"id\":\"frequency\",\"index\":0"));
}

TEST_CASE("Segments reach the device as its sample-accurate setter", "[abi]") {
    AbiDevice gain(*linkedModule(), "sdkReferenceGain");
    const magda_param_segment segment{0, 0.25f, 0.75f, 0};
    CHECK(api().set_param_segments(gain.get(), 0, &segment, 1) == MAGDA_ERR_STATE);
    REQUIRE(gain.prepare(48000.0, 16) == MAGDA_OK);
    CHECK(api().set_param_segments(gain.get(), 0, &segment, 1) == MAGDA_OK);
    CHECK(api().param_value(gain.get(), 0) == 0.25f);
    CHECK(api().set_param_segments(gain.get(), 0, &segment, 0) == MAGDA_ERR_ARGUMENT);
}

TEST_CASE("State round-trips as the device document and a refused one changes nothing", "[abi]") {
    AbiDevice sine(*linkedModule(), "sdkReferenceSine");
    CHECK(sine.state() == "{\n  \"schema\": 2,\n  \"device\": \"sdkReferenceSine\"\n}\n");

    REQUIRE(
        sine.setState(R"({"schema":2,"device":"sdkReferenceSine","props":{"inverted":true}})") ==
        MAGDA_OK);
    const auto inverted = sine.state();
    CHECK(inverted.find("\"inverted\": true") != std::string::npos);
    REQUIRE(sine.setState(inverted) == MAGDA_OK);
    CHECK(sine.state() == inverted);

    CHECK(sine.setState(R"({"schema":2,"device":"sdkReferenceSine","props":{"inverted":"x"}})") ==
          MAGDA_ERR_REJECTED);
    CHECK(sine.lastError() == "inverted is a bool");
    CHECK(sine.setState(R"({"schema":2,"device":"other"})") == MAGDA_ERR_REJECTED);
    CHECK(sine.setState("not json") == MAGDA_ERR_REJECTED);
    CHECK(sine.state() == inverted);
}

TEST_CASE("A future schema is kept verbatim while the device loads its defaults", "[abi]") {
    AbiDevice sine(*linkedModule(), "sdkReferenceSine");
    REQUIRE(
        sine.setState(R"({"schema":2,"device":"sdkReferenceSine","props":{"inverted":true}})") ==
        MAGDA_OK);
    const std::string_view future = R"({"schema":3,"device":"sdkReferenceSine","new":1})";
    CHECK(sine.setState(future) == MAGDA_ERR_REJECTED);
    CHECK(sine.state() == future);

    REQUIRE(sine.prepare(48000.0, 32) == MAGDA_OK);
    AbiDevice plain(*linkedModule(), "sdkReferenceSine");
    REQUIRE(plain.prepare(48000.0, 32) == MAGDA_OK);
    Stereo a(32), b(32);
    sine.process(a.pointers.data(), 2, 32);
    plain.process(b.pointers.data(), 2, 32);
    CHECK(a.left == b.left);
}

TEST_CASE("MIDI out is the device's emission, within the host's budgets", "[abi]") {
    AbiDevice gain(*linkedModule(), "sdkReferenceGain");
    REQUIRE(gain.prepare(48000.0, 16) == MAGDA_OK);
    const std::uint8_t noteOn[] = {0x90, 60, 100};
    const std::uint8_t sysex[] = {0xF0, 1, 2, 3, 0xF7};
    gain.queueMidi(sysex, 5, 40);
    gain.queueMidi(noteOn, 3, 3);
    Stereo audio(48);
    audio.left.assign(48, 1.0f);
    audio.pointers[0] = audio.left.data();
    REQUIRE(gain.process(audio.pointers.data(), 2, 48) == MAGDA_OK);
    CHECK(audio.left[47] == 0.5f);

    REQUIRE(gain.midiOutCount() == 2);
    CHECK(gain.midiOut(0).size == 3);
    CHECK(gain.midiOut(0).sample == 3);
    CHECK(AbiDevice::bytesOf(gain.midiOut(0))[1] == 60);
    CHECK(gain.midiOut(1).size == 5);
    CHECK(gain.midiOut(1).sample == 40);
    CHECK(AbiDevice::bytesOf(gain.midiOut(1))[4] == 0xF7);

    gain.process(audio.pointers.data(), 2, 8);
    CHECK(gain.midiOutCount() == 0);

    std::array<magda_midi_event, 2> in{};
    for (auto& event : in) {
        event.size = 3;
        std::copy(noteOn, noteOn + 3, event.short_data);
    }
    std::array<magda_midi_event, 1> outEvents{};
    magda_midi_in midiIn{in.data(), 2, 0};
    magda_midi_out midiOut{outEvents.data(), 1, 0, nullptr, 0, 0, 0, 0};
    auto p = processOf(audio, 16);
    p.midi_in = &midiIn;
    p.midi_out = &midiOut;
    REQUIRE(api().process(gain.get(), &p) == MAGDA_OK);
    CHECK(midiOut.count == 1);
}

TEST_CASE("Transport and sidechain reach the device as the host passed them", "[abi]") {
    AbiDevice probe(*linkedModule(), "sdkReferenceProbe");
    REQUIRE(probe.prepare(48000.0, 8) == MAGDA_OK);
    Stereo audio(8);
    auto p = processOf(audio, 8);
    REQUIRE(api().process(probe.get(), &p) == MAGDA_OK);
    CHECK(audio.left[0] == -1.0f);
    CHECK(audio.right[0] == -2.0f);

    magda_transport transport{};
    transport.struct_tag = MAGDA_TAG_TRANSPORT;
    transport.struct_size = sizeof(transport);
    transport.tempo_kind = MAGDA_TEMPO_CONSTANT;
    transport.bpm = 120.0;
    transport.block_start_seconds = 1.5;
    std::vector<float> key(8, 0.25f);
    const float* keyChannels[] = {key.data()};
    p.transport = &transport;
    p.sidechain = keyChannels;
    p.sidechain_channels = 1;
    REQUIRE(api().process(probe.get(), &p) == MAGDA_OK);
    CHECK(audio.left[0] == 3.0f);
    CHECK(audio.right[7] == 0.25f);

    const magda_tempo_map map{nullptr, [](void*, double s) { return s * 4.0; },
                              [](void*, double) { return 240.0; }};
    transport.tempo_kind = MAGDA_TEMPO_MAP;
    transport.tempo_map = &map;
    REQUIRE(api().process(probe.get(), &p) == MAGDA_OK);
    CHECK(audio.left[0] == 6.0f);

    transport.tempo_map = nullptr;
    CHECK(api().process(probe.get(), &p) == MAGDA_ERR_ARGUMENT);
    CHECK(api().tail(probe.get()) == -1);
}

TEST_CASE("A split call gives each prepared block its slice of the transport", "[abi]") {
    AbiDevice probe(*linkedModule(), "sdkReferenceProbe");
    REQUIRE(probe.prepare(48000.0, 8) == MAGDA_OK);
    Stereo audio(16);

    magda_transport transport{};
    transport.struct_tag = MAGDA_TAG_TRANSPORT;
    transport.struct_size = sizeof(transport);
    transport.tempo_kind = MAGDA_TEMPO_CONSTANT;
    transport.bpm = 120.0;
    transport.block_start_seconds = 1.5;
    transport.block_end_seconds = 1.5 + 16 / 48000.0;
    REQUIRE(probe.process(audio.pointers.data(), 2, 16, &transport) == MAGDA_OK);
    CHECK(audio.left[0] == 3.0f);
    CHECK(audio.left[8] == Catch::Approx(2.0 * (1.5 + 8 / 48000.0)));
}

TEST_CASE("Notifications are pulled, announced outside set_state, and carry the patch", "[abi]") {
    struct Counter {
        int calls = 0;
    } counter;
    magda_host host{};
    host.struct_tag = MAGDA_TAG_HOST;
    host.struct_size = sizeof(host);
    host.context = &counter;
    host.notify = [](void* context, magda_device*) { ++static_cast<Counter*>(context)->calls; };

    AbiDevice probe(*linkedModule(), "sdkReferenceProbe", &host);
    auto* handle = probe.get();
    REQUIRE(probe.prepare(48000.0, 16) == MAGDA_OK);
    CHECK(api().param_count(handle) == 1);

    REQUIRE(api().reset(handle) == MAGDA_OK);
    CHECK(counter.calls == 1);
    CHECK(api().take_notifications(handle) == MAGDA_NOTIFY_STATE_CHANGED);
    std::array<char, 256> text{};
    std::int32_t size = 0;
    REQUIRE(api().take_state_patch(handle, text.data(), text.size(), &size) == MAGDA_OK);
    CHECK(std::string_view(text.data(), static_cast<std::size_t>(size)).find("\"resets\": 1") !=
          std::string_view::npos);
    CHECK(api().take_state_patch(handle, text.data(), text.size(), &size) == MAGDA_ERR_STATE);
    CHECK(probe.state().find("\"resets\": 1") != std::string::npos);

    REQUIRE(probe.setState(
                R"({"schema":2,"device":"sdkReferenceProbe","props":{"slots":3,"latency":12}})") ==
            MAGDA_OK);
    CHECK(counter.calls == 1);
    CHECK(api().latency(handle) == 12);
    CHECK(api().param_count(handle) == 3);
    CHECK(api().take_notifications(handle) ==
          (MAGDA_NOTIFY_PARAMETERS_CHANGED | MAGDA_NOTIFY_PROPERTIES_CHANGED));
    CHECK(api().take_notifications(handle) == 0);
    CHECK(probe.manifest().find("\"id\":\"slot_2\"") != std::string::npos);
    CHECK(typeManifest("sdkReferenceProbe").find("slot_2") == std::string::npos);
}

TEST_CASE("Version 1 structs are read up to their size and refused below it", "[abi]") {
    AbiDevice sine(*linkedModule(), "sdkReferenceSine");
    struct Larger {
        magda_prepare v1;
        double later;
    } larger{};
    larger.v1.struct_tag = MAGDA_TAG_PREPARE;
    larger.v1.struct_size = sizeof(larger);
    larger.v1.sample_rate = 48000.0;
    larger.v1.max_frames = 32;
    larger.later = 7.0;
    CHECK(api().prepare(sine.get(), &larger.v1) == MAGDA_OK);

    larger.v1.struct_size = sizeof(magda_prepare) - 4;
    CHECK(api().prepare(sine.get(), &larger.v1) == MAGDA_ERR_ARGUMENT);
    CHECK(!sine.lastError().empty());
}
