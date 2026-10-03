#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include "magda/sdk/abi/magda_device.h"

namespace {

struct Handle {
    explicit Handle(const char* type) : device(magda_device_create(type)) {}
    ~Handle() {
        magda_device_destroy(device);
    }
    magda_device* device;
};

struct Stereo {
    explicit Stereo(int frames)
        : left(frames), right(frames), pointers{left.data(), right.data()} {}
    std::vector<float> left, right;
    std::array<float*, 2> pointers;
};

int setState(magda_device* device, std::string_view json) {
    return magda_device_set_state(device, json.data(), static_cast<int>(json.size()));
}

}  // namespace

TEST_CASE("The module lists its device types and creates only those", "[abi]") {
    CHECK(magda_device_abi_version() == MAGDA_DEVICE_ABI_VERSION);
    REQUIRE(magda_device_type_count() == 2);
    CHECK(std::string_view(magda_device_type_at(0)) == "sdkReferenceSine");
    CHECK(magda_device_type_at(2) == nullptr);
    CHECK(magda_device_create("nope") == nullptr);
    CHECK(magda_device_create(nullptr) == nullptr);
}

TEST_CASE("Process before prepare is a lifecycle error", "[abi]") {
    Handle sine("sdkReferenceSine");
    Stereo audio(16);
    CHECK(magda_device_process(sine.device, audio.pointers.data(), 2, 16) == MAGDA_ERR_STATE);
}

TEST_CASE("A call longer than the prepared block renders the same as block-sized calls", "[abi]") {
    Handle whole("sdkReferenceSine");
    Handle blocks("sdkReferenceSine");
    REQUIRE(magda_device_prepare(whole.device, 48000.0, 64) == MAGDA_OK);
    REQUIRE(magda_device_prepare(blocks.device, 48000.0, 64) == MAGDA_OK);

    const std::uint8_t noteOn[] = {0x90, 81, 100};
    Stereo a(200), b(200);
    magda_device_midi(whole.device, noteOn, 3, 150);
    REQUIRE(magda_device_process(whole.device, a.pointers.data(), 2, 200) == MAGDA_OK);

    for (int start = 0; start < 200; start += 64) {
        const int count = std::min(64, 200 - start);
        if (start <= 150 && 150 < start + count)
            magda_device_midi(blocks.device, noteOn, 3, 150 - start);
        std::array<float*, 2> pointers{b.left.data() + start, b.right.data() + start};
        magda_device_process(blocks.device, pointers.data(), 2, count);
    }
    CHECK(a.left == b.left);
    CHECK(a.right == a.left);
}

TEST_CASE("Parameters are normalized and convert through the manifest's reference", "[abi]") {
    Handle sine("sdkReferenceSine");
    REQUIRE(magda_device_param_count(sine.device) == 2);
    CHECK(magda_device_param_to_real(sine.device, 0, magda_device_get_param(sine.device, 0)) ==
          Catch::Approx(440.0f).epsilon(1e-4));
    CHECK(magda_device_set_param(sine.device, 1, 2.0f) == MAGDA_OK);
    CHECK(magda_device_get_param(sine.device, 1) == 1.0f);
    CHECK(magda_device_set_param(sine.device, 5, 0.5f) == MAGDA_ERR_ARGUMENT);
    CHECK(magda_device_param_to_normalized(sine.device, 1, -30.0f) == Catch::Approx(0.5f));
}

TEST_CASE("State round-trips as the device document and a refused one changes nothing", "[abi]") {
    Handle sine("sdkReferenceSine");
    CHECK(std::string_view(magda_device_get_state(sine.device)) ==
          "{\n  \"schema\": 2,\n  \"device\": \"sdkReferenceSine\"\n}\n");

    REQUIRE(setState(sine.device,
                     R"({"schema":2,"device":"sdkReferenceSine","props":{"inverted":true}})") ==
            MAGDA_OK);
    const std::string inverted = magda_device_get_state(sine.device);
    CHECK(inverted.find("\"inverted\": true") != std::string::npos);
    REQUIRE(setState(sine.device, inverted) == MAGDA_OK);
    CHECK(magda_device_get_state(sine.device) == inverted);

    CHECK(setState(sine.device,
                   R"({"schema":2,"device":"sdkReferenceSine","props":{"inverted":"x"}})") ==
          MAGDA_ERR_REJECTED);
    CHECK(std::string_view(magda_device_last_error(sine.device)) == "inverted is a bool");
    CHECK(setState(sine.device, R"({"schema":2,"device":"other"})") == MAGDA_ERR_REJECTED);
    CHECK(setState(sine.device, "not json") == MAGDA_ERR_REJECTED);
    CHECK(magda_device_get_state(sine.device) == inverted);
}

TEST_CASE("A future schema is kept verbatim while the device loads its defaults", "[abi]") {
    Handle sine("sdkReferenceSine");
    const std::string_view future = R"({"schema":3,"device":"sdkReferenceSine","new":1})";
    CHECK(setState(sine.device, future) == MAGDA_ERR_REJECTED);
    CHECK(std::string_view(magda_device_get_state(sine.device)) == future);
}

TEST_CASE("Inverted state flips the rendered polarity", "[abi]") {
    Handle plain("sdkReferenceSine");
    Handle inverted("sdkReferenceSine");
    REQUIRE(setState(inverted.device,
                     R"({"schema":2,"device":"sdkReferenceSine","props":{"inverted":true}})") ==
            MAGDA_OK);
    magda_device_prepare(plain.device, 48000.0, 32);
    magda_device_prepare(inverted.device, 48000.0, 32);
    Stereo a(32), b(32);
    magda_device_process(plain.device, a.pointers.data(), 2, 32);
    magda_device_process(inverted.device, b.pointers.data(), 2, 32);
    CHECK(a.left[5] != 0.0f);
    CHECK(a.left[5] == -b.left[5]);
}

TEST_CASE("The manifest is the device's, resolved", "[abi]") {
    Handle sine("sdkReferenceSine");
    const std::string manifest = magda_device_get_manifest(sine.device);
    CHECK(manifest.find("\"deviceType\":\"sdkReferenceSine\"") != std::string::npos);
    CHECK(manifest.find("\"id\":\"frequency\"") != std::string::npos);
}

TEST_CASE("MIDI out is the device's emission, at the frames it addressed", "[abi]") {
    Handle gain("sdkReferenceGain");
    magda_device_prepare(gain.device, 48000.0, 16);
    const std::uint8_t noteOn[] = {0x90, 60, 100};
    const std::uint8_t sysex[] = {0xF0, 1, 2, 3, 0xF7};
    magda_device_midi(gain.device, sysex, 5, 40);
    magda_device_midi(gain.device, noteOn, 3, 3);
    Stereo audio(48);
    audio.left.assign(48, 1.0f);
    audio.pointers[0] = audio.left.data();
    REQUIRE(magda_device_process(gain.device, audio.pointers.data(), 2, 48) == MAGDA_OK);
    CHECK(audio.left[47] == 0.5f);

    REQUIRE(magda_device_midi_out_count(gain.device) == 2);
    int size = 0, offset = 0;
    const auto* first = magda_device_midi_out_at(gain.device, 0, &size, &offset);
    CHECK(size == 3);
    CHECK(offset == 3);
    CHECK(first[1] == 60);
    const auto* second = magda_device_midi_out_at(gain.device, 1, &size, &offset);
    CHECK(size == 5);
    CHECK(offset == 40);
    CHECK(second[4] == 0xF7);

    magda_device_process(gain.device, audio.pointers.data(), 2, 8);
    CHECK(magda_device_midi_out_count(gain.device) == 0);
}

TEST_CASE("The MIDI queue is bounded and says so", "[abi]") {
    Handle gain("sdkReferenceGain");
    const std::uint8_t noteOn[] = {0x90, 60, 100};
    int added = 0;
    while (magda_device_midi(gain.device, noteOn, 3, 0) == MAGDA_OK)
        ++added;
    CHECK(added == 1024);
}

TEST_CASE("Analysis reaches a device that offers it and is refused by one that does not", "[abi]") {
    Handle sine("sdkReferenceSine");
    const std::vector<float> ones(100, 1.0f);
    const char* result = magda_device_analyze(sine.device, ones.data(), 100, 48000.0);
    REQUIRE(result != nullptr);
    CHECK(std::string_view(result).starts_with("{\"rms\":1.0"));

    Handle gain("sdkReferenceGain");
    CHECK(magda_device_analyze(gain.device, ones.data(), 100, 48000.0) == nullptr);
    CHECK(std::string_view(magda_device_last_error(gain.device)) == "the device does not analyze");
}
