// The wasm module has no entry point. Its exports are this flat glue over the C ABI table
// (magda/sdk/abi/magda_device.h), so the JS side never builds an ABI struct.

#include <string>

#include "magda/sdk/abi/AbiDevice.hpp"

using magda::sdk::host::AbiDevice;

namespace {

struct WamDevice {
    explicit WamDevice(const char* type) : device(*magda::sdk::host::linkedModule(), type) {}
    AbiDevice device;
    std::string text;
    double sampleRate = 0.0;
};

/// Null for empty text, so JS reads it as absent.
const char* textOrNull(WamDevice* wam, std::string text) {
    wam->text = std::move(text);
    return wam->text.empty() ? nullptr : wam->text.c_str();
}

const magda_module& module() {
    return *magda::sdk::host::linkedModule();
}

}  // namespace

extern "C" {

int magda_wam_type_count(void) {
    return module().device_type_count;
}

const char* magda_wam_type_at(int index) {
    if (index < 0 || index >= module().device_type_count)
        return nullptr;
    return module().device_types[index].id;
}

WamDevice* magda_wam_create(const char* type) {
    auto* wam = new WamDevice(type);
    if (wam->device)
        return wam;
    delete wam;
    return nullptr;
}

void magda_wam_destroy(WamDevice* wam) {
    delete wam;
}

int magda_wam_prepare(WamDevice* wam, double sampleRate, int maxBlockSize) {
    const auto status = wam->device.prepare(sampleRate, maxBlockSize);
    if (status == MAGDA_OK)
        wam->sampleRate = sampleRate;
    return status;
}

void magda_wam_release(WamDevice* wam) {
    wam->device.api().release(wam->device.get());
}

void magda_wam_reset(WamDevice* wam) {
    wam->device.api().reset(wam->device.get());
}

int magda_wam_latency(WamDevice* wam) {
    return wam->device.api().latency(wam->device.get());
}

/// Samples, or -1 for a tail that never decays; a double so JS needs no BigInt.
double magda_wam_tail(WamDevice* wam) {
    return static_cast<double>(wam->device.api().tail(wam->device.get()));
}

/**
 * @brief Audio. @p transportFlags below zero passes no transport; otherwise it holds the
 * MAGDA_TRANSPORT_* flags, the block starts at @p startSeconds, and @p bpm above zero is a
 * constant tempo.
 */
int magda_wam_process(WamDevice* wam, float* const* channels, int numChannels, int numFrames,
                      int transportFlags, double startSeconds, double bpm) {
    if (transportFlags < 0 || wam->sampleRate <= 0.0)
        return wam->device.process(channels, numChannels, numFrames);

    magda_transport transport{};
    transport.struct_tag = MAGDA_TAG_TRANSPORT;
    transport.struct_size = sizeof(transport);
    transport.flags = static_cast<std::uint32_t>(transportFlags);
    transport.block_start_seconds = startSeconds;
    transport.block_end_seconds = startSeconds + numFrames / wam->sampleRate;
    if (bpm > 0.0) {
        transport.tempo_kind = MAGDA_TEMPO_CONSTANT;
        transport.bpm = bpm;
    }
    return wam->device.process(channels, numChannels, numFrames, &transport);
}

int magda_wam_set_param(WamDevice* wam, int slot, float normalized) {
    return wam->device.api().set_param(wam->device.get(), slot, normalized);
}

float magda_wam_get_param(WamDevice* wam, int slot) {
    return wam->device.api().param_value(wam->device.get(), slot);
}

int magda_wam_param_count(WamDevice* wam) {
    return wam->device.parameterCount();
}

int magda_wam_param_offered(WamDevice* wam, int slot) {
    return wam->device.api().param_offered(wam->device.get(), slot);
}

/// The slot's manifest entry, null past the instance's count. Valid until the next text call.
const char* magda_wam_param_descriptor(WamDevice* wam, int slot) {
    return textOrNull(wam, wam->device.parameterDescriptor(slot));
}

float magda_wam_param_to_real(WamDevice* wam, int slot, float normalized) {
    return wam->device.api().param_to_real(wam->device.get(), slot, normalized);
}

float magda_wam_param_to_normalized(WamDevice* wam, int slot, float real) {
    return wam->device.api().param_to_normalized(wam->device.get(), slot, real);
}

int magda_wam_midi(WamDevice* wam, const std::uint8_t* bytes, int size, int sampleOffset) {
    return wam->device.queueMidi(bytes, size, sampleOffset) ? MAGDA_OK : MAGDA_ERR_FULL;
}

int magda_wam_midi_out_count(WamDevice* wam) {
    return wam->device.midiOutCount();
}

const std::uint8_t* magda_wam_midi_out_at(WamDevice* wam, int index, int* size, int* sampleOffset) {
    if (index < 0 || index >= wam->device.midiOutCount())
        return nullptr;
    const auto& event = wam->device.midiOut(index);
    *size = static_cast<int>(event.size);
    *sampleOffset = event.sample;
    return AbiDevice::bytesOf(event);
}

/// Text valid until the next text-returning call on @p wam.
const char* magda_wam_get_state(WamDevice* wam) {
    wam->text = wam->device.state();
    return wam->text.c_str();
}

int magda_wam_set_state(WamDevice* wam, const char* json, int size) {
    return wam->device.setState(std::string_view(json, static_cast<std::size_t>(size)));
}

unsigned magda_wam_take_notifications(WamDevice* wam) {
    return wam->device.api().take_notifications(wam->device.get());
}

const char* magda_wam_take_state_patch(WamDevice* wam) {
    return textOrNull(wam, wam->device.takeStatePatch());
}

const char* magda_wam_get_manifest(WamDevice* wam) {
    wam->text = wam->device.manifest();
    return wam->text.c_str();
}

const char* magda_wam_last_error(WamDevice* wam) {
    wam->text = wam->device.lastError();
    return wam->text.c_str();
}

}  // extern "C"
