#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "magda/sdk/abi/DeviceModule.hpp"
#include "magda/sdk/abi/magda_device.h"
#include "magda/sdk/device/Analysis.hpp"
#include "magda/sdk/device/ParameterManifest.hpp"
#include "magda/sdk/state/StateCodec.hpp"

namespace {

using namespace magda::sdk;

constexpr std::size_t kMidiEventCapacity = 1024;
constexpr std::size_t kSysexByteCapacity = 64 * 1024;
constexpr std::size_t kMaxChannels = 64;

/// Bounded MIDI storage: events plus the bytes of their long messages. Never reallocates.
class EventStore {
  public:
    EventStore() {
        events_.reserve(kMidiEventCapacity);
        bytes_.resize(kSysexByteCapacity);
    }

    bool add(const MidiEvent& event) {
        if (events_.size() >= kMidiEventCapacity)
            return false;
        auto stored = event;
        if (event.isSysex()) {
            if (event.longSize > bytes_.size() - bytesUsed_)
                return false;
            std::memcpy(bytes_.data() + bytesUsed_, event.longData, event.longSize);
            stored.longData = bytes_.data() + bytesUsed_;
            bytesUsed_ += event.longSize;
        }
        events_.push_back(stored);
        return true;
    }

    void clear() {
        events_.clear();
        bytesUsed_ = 0;
    }

    /// Stable insertion sort by sample: allocation-free and linear on input already in order.
    void sortBySample() {
        for (std::size_t i = 1; i < events_.size(); ++i)
            for (auto j = i; j > 0 && events_[j - 1].sample > events_[j].sample; --j)
                std::swap(events_[j - 1], events_[j]);
    }

    std::vector<MidiEvent>& events() {
        return events_;
    }
    const std::vector<MidiEvent>& events() const {
        return events_;
    }

  private:
    std::vector<MidiEvent> events_;
    std::vector<std::uint8_t> bytes_;
    std::size_t bytesUsed_ = 0;
};

class SpanInput final : public MidiInput {
  public:
    int size() const override {
        return static_cast<int>(events.size());
    }
    const MidiEvent& event(int index) const override {
        return events[static_cast<std::size_t>(index)];
    }
    bool isAllNotesOff() const override {
        return false;
    }

    std::span<const MidiEvent> events;
};

/// The device's output across every chunk of one process call, rebased to the call's frames.
class ChunkOutput final : public MidiOutput {
  public:
    bool addEvent(const MidiEvent& event) override {
        auto rebased = event;
        rebased.sample = std::clamp(event.sample, 0, std::max(0, chunkFrames - 1)) + chunkStart;
        return store.add(rebased);
    }
    void setAllNotesOff(bool) override {}

    EventStore store;
    int chunkStart = 0;
    int chunkFrames = 0;
};

}  // namespace

struct magda_device final : DeviceHost {
    std::unique_ptr<Device> device;
    DeviceProperties properties;
    StateDocument document;
    /// A newer schema's text, kept verbatim and returned as the state (docs/device-state.md).
    std::optional<std::string> futureText;

    bool prepared = false;
    bool rebuildPending = false;
    PrepareContext prepareContext;

    EventStore pendingMidi;
    std::vector<MidiEvent> chunkMidi;
    SpanInput chunkInput;
    ChunkOutput output;
    std::array<float*, kMaxChannels> chunkChannels{};

    std::string text;
    std::string error;

    void stateChanged(StateNode patch) override {
        auto& root = document.root;
        for (const auto& property : patch.properties())
            root.set(property.key, property.value);
        for (const auto& child : patch.children()) {
            for (std::size_t i = root.children().size(); i > 0; --i)
                if (root.children()[i - 1].type() == child.type())
                    root.removeChild(i - 1);
        }
        for (auto& child : patch.children())
            root.addChild(std::move(child));
    }

    void rebuildRequired() override {
        rebuildPending = true;
    }

    void prepareDevice(const PrepareContext& context) {
        if (prepared)
            device->release();
        properties = device->properties();
        prepareContext = context;
        device->prepare(context);
        chunkMidi.reserve(kMidiEventCapacity);
        prepared = true;
        rebuildPending = false;
    }

    void rebuildIfAsked() {
        if (rebuildPending && prepared)
            prepareDevice(prepareContext);
    }

    bool routesMidi() const {
        return properties.takesMidiInput || properties.producesMidi || properties.forwardsMidiInput;
    }

    int fail(int code, std::string message) {
        error = std::move(message);
        return code;
    }

    const char* failText(std::string message) {
        error = std::move(message);
        return nullptr;
    }

    const char* returnText(std::string value) {
        text = std::move(value);
        error.clear();
        return text.c_str();
    }
};

namespace {

const std::vector<std::string>& typeNames() {
    static const auto names = [] {
        std::vector<std::string> result;
        for (const auto& factory : abi::moduleDevices())
            result.emplace_back(factory.deviceType);
        return result;
    }();
    return names;
}

bool validSlot(const magda_device* handle, int slot) {
    return handle != nullptr && slot >= 0 && slot < handle->device->parameterCount();
}

}  // namespace

extern "C" {

int magda_device_abi_version(void) {
    return MAGDA_DEVICE_ABI_VERSION;
}

int magda_device_type_count(void) {
    return static_cast<int>(typeNames().size());
}

const char* magda_device_type_at(int index) {
    const auto& names = typeNames();
    if (index < 0 || static_cast<std::size_t>(index) >= names.size())
        return nullptr;
    return names[static_cast<std::size_t>(index)].c_str();
}

magda_device* magda_device_create(const char* device_type) {
    if (device_type == nullptr)
        return nullptr;
    const std::string_view wanted(device_type);
    for (const auto& factory : abi::moduleDevices()) {
        if (factory.deviceType != wanted || factory.create == nullptr)
            continue;
        auto device = factory.create();
        if (!device)
            return nullptr;
        auto* handle = new magda_device;
        handle->device = std::move(device);
        handle->device->setHost(handle);
        handle->properties = handle->device->properties();
        handle->document.deviceType = handle->properties.pluginId.empty()
                                          ? std::string(factory.deviceType)
                                          : handle->properties.pluginId;
        return handle;
    }
    return nullptr;
}

void magda_device_destroy(magda_device* handle) {
    if (handle == nullptr)
        return;
    if (handle->prepared)
        handle->device->release();
    handle->device->setHost(nullptr);
    delete handle;
}

int magda_device_prepare(magda_device* handle, double sample_rate, int max_block_size) {
    if (handle == nullptr || !(sample_rate > 0.0) || max_block_size <= 0)
        return MAGDA_ERR_ARGUMENT;
    handle->prepareDevice({sample_rate, max_block_size});
    return MAGDA_OK;
}

void magda_device_reset(magda_device* handle) {
    if (handle != nullptr)
        handle->device->reset();
}

int magda_device_latency(const magda_device* handle) {
    return handle != nullptr && handle->prepared ? handle->device->latencySamples() : 0;
}

int magda_device_process(magda_device* handle, float* const* channels, int num_channels,
                         int num_frames) {
    if (handle == nullptr || num_frames < 0 || num_channels < 0 ||
        (num_channels > 0 && channels == nullptr))
        return MAGDA_ERR_ARGUMENT;
    if (!handle->prepared)
        return MAGDA_ERR_STATE;

    const int blockSize = handle->prepareContext.maximumBlockSize;
    const bool chunked = num_frames > blockSize;
    if (chunked && static_cast<std::size_t>(num_channels) > kMaxChannels)
        return MAGDA_ERR_ARGUMENT;

    auto& pending = handle->pendingMidi;
    handle->output.store.clear();
    if (num_frames == 0) {
        pending.clear();
        return MAGDA_OK;
    }

    for (auto& event : pending.events())
        event.sample = std::clamp(event.sample, 0, std::max(0, num_frames - 1));
    pending.sortBySample();

    const bool midi = handle->routesMidi();
    std::size_t nextEvent = 0;
    for (int start = 0; start < num_frames; start += blockSize) {
        const int frames = std::min(blockSize, num_frames - start);

        handle->chunkMidi.clear();
        const auto& events = pending.events();
        while (nextEvent < events.size() && events[nextEvent].sample < start + frames) {
            auto event = events[nextEvent++];
            event.sample -= start;
            handle->chunkMidi.push_back(event);
        }
        handle->chunkInput.events = handle->chunkMidi;
        handle->output.chunkStart = start;
        handle->output.chunkFrames = frames;

        float* const* view = channels;
        if (chunked) {
            for (int c = 0; c < num_channels; ++c)
                handle->chunkChannels[static_cast<std::size_t>(c)] = channels[c] + start;
            view = handle->chunkChannels.data();
        }

        ProcessContext context;
        context.audio = magda::BufferView(view, num_channels, frames);
        if (midi) {
            context.midiIn = &handle->chunkInput;
            context.midiOut = &handle->output;
        }
        handle->device->process(context);
    }

    pending.clear();
    return MAGDA_OK;
}

int magda_device_set_param(magda_device* handle, int slot, float normalized) {
    if (!validSlot(handle, slot))
        return MAGDA_ERR_ARGUMENT;
    handle->device->setParameterValue(slot, std::clamp(normalized, 0.0f, 1.0f));
    return MAGDA_OK;
}

float magda_device_get_param(const magda_device* handle, int slot) {
    return validSlot(handle, slot) ? handle->device->parameterValue(slot) : 0.0f;
}

int magda_device_param_count(const magda_device* handle) {
    return handle != nullptr ? handle->device->parameterCount() : 0;
}

float magda_device_param_to_real(const magda_device* handle, int slot, float normalized) {
    if (!validSlot(handle, slot))
        return 0.0f;
    return normalizedToReal(normalized, domainOf(handle->device->parameterDescriptor(slot)));
}

float magda_device_param_to_normalized(const magda_device* handle, int slot, float real) {
    if (!validSlot(handle, slot))
        return 0.0f;
    return realToNormalized(real, domainOf(handle->device->parameterDescriptor(slot)));
}

int magda_device_midi(magda_device* handle, const uint8_t* bytes, int size, int sample_offset) {
    if (handle == nullptr || bytes == nullptr || size <= 0)
        return MAGDA_ERR_ARGUMENT;
    const auto event =
        MidiEvent::fromBytes(bytes, static_cast<std::uint32_t>(size), std::max(0, sample_offset));
    return handle->pendingMidi.add(event) ? MAGDA_OK : MAGDA_ERR_FULL;
}

int magda_device_midi_out_count(const magda_device* handle) {
    return handle != nullptr ? static_cast<int>(handle->output.store.events().size()) : 0;
}

const uint8_t* magda_device_midi_out_at(const magda_device* handle, int index, int* size,
                                        int* sample_offset) {
    if (handle == nullptr || index < 0 || index >= magda_device_midi_out_count(handle))
        return nullptr;
    const auto& event = handle->output.store.events()[static_cast<std::size_t>(index)];
    if (size != nullptr)
        *size = static_cast<int>(event.size());
    if (sample_offset != nullptr)
        *sample_offset = event.sample;
    return event.data();
}

const char* magda_device_get_state(magda_device* handle) {
    if (handle == nullptr)
        return nullptr;
    if (handle->futureText)
        return handle->returnText(*handle->futureText);
    std::string error;
    auto json = encodeDocument(handle->document, &error);
    if (!json)
        return handle->failText(std::move(error));
    return handle->returnText(std::move(*json));
}

int magda_device_set_state(magda_device* handle, const char* json, int size) {
    if (handle == nullptr || json == nullptr || size < 0)
        return MAGDA_ERR_ARGUMENT;
    const std::string_view text(json, static_cast<std::size_t>(size));

    if (isFutureSchema(text)) {
        handle->futureText = std::string(text);
        handle->document.root = {};
        (void)handle->device->restoreState(handle->document.root);
        handle->rebuildIfAsked();
        return handle->fail(MAGDA_ERR_REJECTED,
                            "state is from a newer schema: kept verbatim, defaults loaded");
    }

    auto decoded = decodeDocument(text);
    if (!decoded.ok())
        return handle->fail(MAGDA_ERR_REJECTED, std::move(decoded.message));
    if (decoded.document->deviceType != handle->document.deviceType)
        return handle->fail(MAGDA_ERR_REJECTED,
                            "state belongs to device \"" + decoded.document->deviceType + "\"");

    const auto restored = handle->device->restoreState(decoded.document->root);
    if (!restored.ok) {
        handle->rebuildIfAsked();
        return handle->fail(MAGDA_ERR_REJECTED, restored.message);
    }
    handle->document = std::move(*decoded.document);
    handle->futureText.reset();
    handle->rebuildIfAsked();
    handle->error.clear();
    return MAGDA_OK;
}

const char* magda_device_get_manifest(magda_device* handle) {
    if (handle == nullptr)
        return nullptr;
    std::string error;
    auto json = writeManifest(buildManifest(*handle->device), error);
    if (!json)
        return handle->failText(std::move(error));
    return handle->returnText(std::move(*json));
}

const char* magda_device_analyze(magda_device* handle, const float* mono, int num_samples,
                                 double sample_rate) {
    if (handle == nullptr || (mono == nullptr && num_samples > 0) || num_samples < 0 ||
        !(sample_rate > 0.0))
        return nullptr;
    auto* analyzing = dynamic_cast<AnalyzingDevice*>(handle->device.get());
    if (analyzing == nullptr)
        return handle->failText("the device does not analyze");
    auto result = analyzing->analyze(
        std::span<const float>(mono, static_cast<std::size_t>(num_samples)), sample_rate);
    handle->rebuildIfAsked();
    if (!result.ok)
        return handle->failText(std::move(result.message));
    return handle->returnText(std::move(result.json));
}

const char* magda_device_last_error(const magda_device* handle) {
    return handle != nullptr ? handle->error.c_str() : "";
}

}  // extern "C"
