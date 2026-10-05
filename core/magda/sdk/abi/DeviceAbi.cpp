#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "magda/sdk/abi/DeviceModule.hpp"
#include "magda/sdk/abi/magda_device.h"
#include "magda/sdk/device/ParameterManifest.hpp"
#include "magda/sdk/state/StateCodec.hpp"
#include "magda/sdk/version.hpp"

namespace {

using namespace magda::sdk;

constexpr int kDefaultMaxMidiEvents = 1024;

/**
 * @brief Reads a tagged struct the caller wrote: the tag must match and struct_size must cover
 * version 1. Fields past struct_size read as zero.
 */
template <typename T> bool readTagged(const T* in, std::uint32_t tag, T& out) {
    if (in == nullptr || in->struct_tag != tag || in->struct_size < sizeof(T))
        return false;
    out = {};
    std::memcpy(&out, in, sizeof(T));
    return true;
}

/// Writes the fields of @p value that the caller's struct_size has room for.
template <typename T> bool writeTagged(T* out, std::uint32_t tag, const T& value) {
    if (out == nullptr || out->struct_tag != tag || out->struct_size < sizeof(T))
        return false;
    const auto header = sizeof(out->struct_tag) + sizeof(out->struct_size);
    std::memcpy(reinterpret_cast<char*>(out) + header,
                reinterpret_cast<const char*>(&value) + header, sizeof(T) - header);
    return true;
}

class ArrayInput final : public MidiInput {
  public:
    int size() const override {
        return count;
    }
    const MidiEvent& event(int index) const override {
        return events[static_cast<std::size_t>(index)];
    }
    bool isAllNotesOff() const override {
        return allNotesOff;
    }

    std::vector<MidiEvent> events;
    int count = 0;
    bool allNotesOff = false;
};

/// Writes into the host's magda_midi_out, within its event and byte budgets.
class HostOutput final : public MidiOutput {
  public:
    bool addEvent(const MidiEvent& event) override {
        if (out == nullptr || out->count >= out->capacity)
            return false;
        magda_midi_event stored{};
        stored.sample = std::clamp(event.sample, 0, std::max(0, frames - 1));
        stored.fraction = event.fraction;
        stored.source_id = event.sourceId;
        stored.size = event.size();
        if (event.isSysex()) {
            if (out->bytes == nullptr || event.longSize > out->byte_capacity - out->bytes_used)
                return false;
            auto* bytes = out->bytes + out->bytes_used;
            std::memcpy(bytes, event.longData, event.longSize);
            out->bytes_used += event.longSize;
            stored.data = bytes;
        } else {
            std::memcpy(stored.short_data, event.bytes.data(), event.numBytes);
        }
        out->events[out->count++] = stored;
        return true;
    }

    void setAllNotesOff(bool allNotesOff) override {
        if (out == nullptr)
            return;
        if (allNotesOff)
            out->flags |= MAGDA_MIDI_ALL_NOTES_OFF;
        else
            out->flags &= ~static_cast<std::uint32_t>(MAGDA_MIDI_ALL_NOTES_OFF);
    }

    magda_midi_out* out = nullptr;
    int frames = 0;
};

class ConstantTempo final : public TempoMap {
  public:
    double beatsAtSeconds(double seconds) const override {
        return seconds * bpm / 60.0;
    }
    double bpmAtSeconds(double) const override {
        return bpm;
    }
    double bpm = 120.0;
};

class HostTempoMap final : public TempoMap {
  public:
    double beatsAtSeconds(double seconds) const override {
        return map->beats_at_seconds(map->context, seconds);
    }
    double bpmAtSeconds(double seconds) const override {
        return map->bpm_at_seconds(map->context, seconds);
    }
    const magda_tempo_map* map = nullptr;
};

/// The device types and their manifests, built once on the first entry call.
struct ModuleTables {
    std::vector<std::string> ids, names, manifests;
    std::vector<magda_device_type> types;
};

}  // namespace

struct magda_device final : DeviceHost {
    std::unique_ptr<Device> device;
    magda_host host{};
    bool hasHost = false;

    DeviceProperties properties;
    StateDocument document;
    /// A newer schema's text, kept verbatim and returned as the state (docs/device-state.md).
    std::optional<std::string> futureText;
    std::optional<StateNode> pendingPatch;

    bool prepared = false;
    bool rebuildPending = false;
    PrepareContext prepareContext;

    std::atomic<int> parameterCount{0};
    std::vector<ParameterDomain> domains;

    ArrayInput input;
    HostOutput output;
    std::vector<ParameterSegment> segments;
    ConstantTempo constantTempo;
    HostTempoMap hostTempo;

    std::uint32_t pending = 0;
    std::uint32_t unannounced = 0;
    int callDepth = 0;
    bool inSetState = false;

    std::string error;

    void stateChanged(StateNode patch) override {
        if (!futureText)
            mergePatch(document.root, patch);
        if (pendingPatch)
            mergePatch(*pendingPatch, patch);
        else
            pendingPatch = std::move(patch);
        raise(MAGDA_NOTIFY_STATE_CHANGED);
    }

    void rebuildRequired() override {
        rebuildPending = true;
        raise(MAGDA_NOTIFY_PROPERTIES_CHANGED);
    }

    /// Properties overwrite; each child replaces the target's children of its type.
    static void mergePatch(StateNode& target, const StateNode& patch) {
        for (const auto& property : patch.properties())
            target.set(property.key, property.value);
        for (const auto& child : patch.children())
            for (std::size_t i = target.children().size(); i > 0; --i)
                if (target.children()[i - 1].type() == child.type())
                    target.removeChild(i - 1);
        for (const auto& child : patch.children())
            target.addChild(child);
    }

    void raise(std::uint32_t flags) {
        pending |= flags;
        unannounced |= flags;
        if (callDepth == 0 && !inSetState)
            announce();
    }

    void announce() {
        if (unannounced == 0 || !hasHost || host.notify == nullptr)
            return;
        unannounced = 0;
        host.notify(host.context, this);
    }

    void refreshParameters() {
        const int count = std::max(0, device->parameterCount());
        domains.clear();
        for (int slot = 0; slot < count; ++slot)
            domains.push_back(domainOf(device->parameterDescriptor(slot)));
        parameterCount.store(count, std::memory_order_release);
    }

    void prepareDevice(const PrepareContext& context, int maxMidiEvents) {
        if (prepared)
            device->release();
        properties = device->properties();
        prepareContext = context;
        device->prepare(context);
        input.events.resize(static_cast<std::size_t>(maxMidiEvents));
        segments.resize(static_cast<std::size_t>(context.maximumBlockSize));
        prepared = true;
        rebuildPending = false;
    }

    void rebuildIfAsked() {
        if (!rebuildPending)
            return;
        if (prepared)
            prepareDevice(prepareContext, static_cast<int>(input.events.size()));
        else
            properties = device->properties();
        rebuildPending = false;
        refreshParameters();
    }

    bool routesMidi() const {
        return properties.takesMidiInput || properties.producesMidi || properties.forwardsMidiInput;
    }

    bool validSlot(int slot) const {
        return slot >= 0 && slot < parameterCount.load(std::memory_order_acquire);
    }

    magda_status fail(magda_status code, std::string message) {
        error = std::move(message);
        return code;
    }

    magda_status writeText(std::string_view text, char* buffer, std::int32_t capacity,
                           std::int32_t* size) {
        if (size == nullptr || capacity < 0)
            return fail(MAGDA_ERR_ARGUMENT, "size is null or capacity negative");
        *size = static_cast<std::int32_t>(text.size());
        if (buffer == nullptr || static_cast<std::size_t>(capacity) < text.size() + 1)
            return fail(MAGDA_ERR_BUFFER, "the buffer is smaller than the text");
        std::memcpy(buffer, text.data(), text.size());
        buffer[text.size()] = '\0';
        return MAGDA_OK;
    }
};

namespace {

/// One control call: clears the error, runs an asked-for rebuild on entry and exit, announces.
class ControlCall {
  public:
    explicit ControlCall(magda_device* handle, bool announces = true)
        : handle_(handle), announces_(announces) {
        handle_->error.clear();
        if (handle_->callDepth++ == 0)
            handle_->rebuildIfAsked();
    }
    ~ControlCall() {
        if (--handle_->callDepth > 0)
            return;
        handle_->rebuildIfAsked();
        if (announces_)
            handle_->announce();
    }
    ControlCall(const ControlCall&) = delete;
    ControlCall& operator=(const ControlCall&) = delete;

  private:
    magda_device* handle_;
    bool announces_;
};

const ModuleTables& tables() {
    static const ModuleTables built = [] {
        ModuleTables t;
        for (const auto& factory : abi::moduleDevices()) {
            const auto device = factory.create != nullptr ? factory.create() : nullptr;
            std::string error;
            auto manifest = device ? writeManifest(buildManifest(*device), error)
                                   : std::optional<std::string>{};
            t.ids.emplace_back(factory.deviceType);
            t.names.push_back(device ? device->properties().name : std::string{});
            t.manifests.push_back(manifest.value_or(std::string{}));
        }
        for (std::size_t i = 0; i < t.ids.size(); ++i)
            t.types.push_back({t.ids[i].c_str(),
                               t.names[i].c_str(),
                               t.manifests[i].c_str(),
                               static_cast<std::uint32_t>(t.manifests[i].size()),
                               {}});
        return t;
    }();
    return built;
}

std::uint32_t propertyFlags(const DeviceProperties& p) {
    std::uint32_t flags = 0;
    flags |= p.takesMidiInput ? MAGDA_PROPERTY_TAKES_MIDI : 0u;
    flags |= p.producesMidi ? MAGDA_PROPERTY_PRODUCES_MIDI : 0u;
    flags |= p.forwardsMidiInput ? MAGDA_PROPERTY_FORWARDS_MIDI : 0u;
    flags |= p.takesAudioInput ? MAGDA_PROPERTY_TAKES_AUDIO : 0u;
    flags |= p.isSynth ? MAGDA_PROPERTY_SYNTH : 0u;
    flags |= p.producesAudioWithoutInput ? MAGDA_PROPERTY_AUDIO_WITHOUT_INPUT : 0u;
    flags |= p.sidechain.kind == SidechainPort::Kind::MIDI ? MAGDA_PROPERTY_MIDI_SIDECHAIN : 0u;
    flags |= p.parameterSource == ParameterSource::State ? MAGDA_PROPERTY_STATE_PARAMETERS : 0u;
    return flags;
}

// ---- Control --------------------------------------------------------------------------------

magda_status apiCreate(const char* deviceType, const magda_host* host, magda_device** out) {
    if (out == nullptr)
        return MAGDA_ERR_ARGUMENT;
    *out = nullptr;
    magda_host hostCopy{};
    if (deviceType == nullptr || (host != nullptr && !readTagged(host, MAGDA_TAG_HOST, hostCopy)))
        return MAGDA_ERR_ARGUMENT;
    const std::string_view wanted(deviceType);
    for (const auto& factory : abi::moduleDevices()) {
        if (factory.deviceType != wanted || factory.create == nullptr)
            continue;
        auto device = factory.create();
        if (!device)
            return MAGDA_ERR_UNSUPPORTED;
        auto* handle = new magda_device;
        handle->device = std::move(device);
        handle->host = hostCopy;
        handle->hasHost = host != nullptr;
        handle->device->setHost(handle);
        handle->properties = handle->device->properties();
        handle->document.deviceType = handle->properties.pluginId.empty()
                                          ? std::string(factory.deviceType)
                                          : handle->properties.pluginId;
        handle->refreshParameters();
        *out = handle;
        return MAGDA_OK;
    }
    return MAGDA_ERR_ARGUMENT;
}

void apiDestroy(magda_device* handle) {
    if (handle == nullptr)
        return;
    if (handle->prepared)
        handle->device->release();
    handle->device->setHost(nullptr);
    delete handle;
}

magda_status apiGetProperties(magda_device* handle, magda_properties* out) {
    if (handle == nullptr)
        return MAGDA_ERR_ARGUMENT;
    ControlCall call(handle);
    const auto& p = handle->properties;
    magda_properties value{};
    value.flags = propertyFlags(p);
    value.input_channels = p.inputChannelCount;
    value.output_channels = p.outputChannelCount;
    value.sidechain_channels = p.sidechain.takesAudio() ? p.sidechain.channels : 0;
    value.device_version = p.deviceVersion;
    if (!writeTagged(out, MAGDA_TAG_PROPERTIES, value))
        return handle->fail(MAGDA_ERR_ARGUMENT, "not a version 1 magda_properties");
    return MAGDA_OK;
}

magda_status apiPrepare(magda_device* handle, const magda_prepare* in) {
    if (handle == nullptr)
        return MAGDA_ERR_ARGUMENT;
    ControlCall call(handle);
    magda_prepare p{};
    if (!readTagged(in, MAGDA_TAG_PREPARE, p))
        return handle->fail(MAGDA_ERR_ARGUMENT, "not a version 1 magda_prepare");
    if (!(p.sample_rate > 0.0) || p.max_frames <= 0 || p.max_midi_events < 0)
        return handle->fail(MAGDA_ERR_ARGUMENT, "sample rate, max frames or MIDI capacity");
    handle->prepareDevice({p.sample_rate, p.max_frames},
                          p.max_midi_events > 0 ? p.max_midi_events : kDefaultMaxMidiEvents);
    return MAGDA_OK;
}

void apiRelease(magda_device* handle) {
    if (handle == nullptr)
        return;
    ControlCall call(handle);
    if (handle->prepared)
        handle->device->release();
    handle->prepared = false;
}

magda_status apiReset(magda_device* handle) {
    if (handle == nullptr)
        return MAGDA_ERR_ARGUMENT;
    ControlCall call(handle);
    handle->device->reset();
    return MAGDA_OK;
}

std::int32_t apiLatency(magda_device* handle) {
    if (handle == nullptr)
        return 0;
    ControlCall call(handle);
    return handle->prepared ? handle->device->latencySamples() : 0;
}

std::int64_t apiTail(magda_device* handle) {
    if (handle == nullptr)
        return 0;
    const auto samples = handle->device->tailSamples();
    return samples == kInfiniteTail ? -1 : samples;
}

// ---- Audio ----------------------------------------------------------------------------------

magda_status apiProcess(magda_device* handle, const magda_process* in) {
    magda_process p{};
    if (handle == nullptr || !readTagged(in, MAGDA_TAG_PROCESS, p))
        return MAGDA_ERR_ARGUMENT;
    if (!handle->prepared)
        return MAGDA_ERR_STATE;
    if (p.num_frames < 0 || p.num_frames > handle->prepareContext.maximumBlockSize ||
        p.num_channels < 0 || (p.num_channels > 0 && p.channels == nullptr) ||
        p.sidechain_channels < 0 || p.live_source_count < 0 ||
        (p.live_source_count > 0 && p.live_source_ids == nullptr) ||
        ((p.midi_in == nullptr) != (p.midi_out == nullptr)))
        return MAGDA_ERR_ARGUMENT;

    magda_transport transport{};
    if (p.transport != nullptr) {
        if (!readTagged(p.transport, MAGDA_TAG_TRANSPORT, transport))
            return MAGDA_ERR_ARGUMENT;
        if (transport.tempo_kind == MAGDA_TEMPO_MAP &&
            (transport.tempo_map == nullptr || transport.tempo_map->beats_at_seconds == nullptr ||
             transport.tempo_map->bpm_at_seconds == nullptr))
            return MAGDA_ERR_ARGUMENT;
    }

    auto& input = handle->input;
    input.count = 0;
    input.allNotesOff = false;
    if (p.midi_out != nullptr) {
        auto& out = *p.midi_out;
        if (out.capacity < 0 || (out.capacity > 0 && out.events == nullptr))
            return MAGDA_ERR_ARGUMENT;
        out.count = 0;
        out.bytes_used = 0;
        out.flags = 0;
    }
    if (p.midi_in != nullptr && handle->routesMidi()) {
        const auto& in = *p.midi_in;
        if (in.count < 0 || (in.count > 0 && in.events == nullptr))
            return MAGDA_ERR_ARGUMENT;
        if (static_cast<std::size_t>(in.count) > input.events.size())
            return MAGDA_ERR_FULL;
        const int lastFrame = std::max(0, p.num_frames - 1);
        for (int i = 0; i < in.count; ++i) {
            const auto& e = in.events[i];
            if (i > 0 && e.sample < in.events[i - 1].sample)
                return MAGDA_ERR_ARGUMENT;
            const auto* bytes = e.size <= 3 ? e.short_data : e.data;
            if (bytes == nullptr && e.size > 0)
                return MAGDA_ERR_ARGUMENT;
            input.events[static_cast<std::size_t>(i)] = MidiEvent::fromBytes(
                bytes, e.size, std::clamp(e.sample, 0, lastFrame), e.fraction, e.source_id);
        }
        input.count = in.count;
        input.allNotesOff = (in.flags & MAGDA_MIDI_ALL_NOTES_OFF) != 0;
    }
    if (p.num_frames == 0)
        return MAGDA_OK;

    ProcessContext context;
    context.audio = magda::BufferView(p.channels, p.num_channels, p.num_frames);
    if (p.sidechain != nullptr)
        context.sidechain = magda::ConstBufferView(p.sidechain, p.sidechain_channels, p.num_frames);
    if (p.midi_in != nullptr && handle->routesMidi()) {
        handle->output.out = p.midi_out;
        handle->output.frames = p.num_frames;
        context.midiIn = &input;
        context.midiOut = &handle->output;
    }
    if (p.transport != nullptr) {
        context.timelineStartSeconds = transport.block_start_seconds;
        context.timelineEndSeconds = transport.block_end_seconds;
        context.isPlaying = (transport.flags & MAGDA_TRANSPORT_PLAYING) != 0;
        context.isRendering = (transport.flags & MAGDA_TRANSPORT_RENDERING) != 0;
        if (transport.tempo_kind == MAGDA_TEMPO_CONSTANT) {
            handle->constantTempo.bpm = transport.bpm;
            context.tempoMap = &handle->constantTempo;
        } else if (transport.tempo_kind == MAGDA_TEMPO_MAP) {
            handle->hostTempo.map = transport.tempo_map;
            context.tempoMap = &handle->hostTempo;
        }
    }
    context.liveSourceIds = std::span<const std::uint32_t>(
        p.live_source_ids, static_cast<std::size_t>(p.live_source_count));

    handle->device->process(context);
    handle->output.out = nullptr;
    return MAGDA_OK;
}

magda_status apiSetParam(magda_device* handle, std::int32_t slot, float normalized) {
    if (handle == nullptr || !handle->validSlot(slot) || std::isnan(normalized))
        return MAGDA_ERR_ARGUMENT;
    handle->device->setParameterValue(slot, std::clamp(normalized, 0.0f, 1.0f));
    return MAGDA_OK;
}

magda_status apiSetParamSegments(magda_device* handle, std::int32_t slot,
                                 const magda_param_segment* segments, std::int32_t count) {
    if (handle == nullptr || !handle->validSlot(slot) || count <= 0 || segments == nullptr)
        return MAGDA_ERR_ARGUMENT;
    if (!handle->prepared)
        return MAGDA_ERR_STATE;
    if (static_cast<std::size_t>(count) > handle->segments.size())
        return MAGDA_ERR_FULL;
    for (int i = 0; i < count; ++i)
        handle->segments[static_cast<std::size_t>(i)] = {
            segments[i].start_sample, std::clamp(segments[i].start_value, 0.0f, 1.0f),
            std::clamp(segments[i].end_value, 0.0f, 1.0f)};
    handle->device->setParameterSegments(
        slot, std::span<const ParameterSegment>(handle->segments.data(),
                                                static_cast<std::size_t>(count)));
    return MAGDA_OK;
}

// ---- Parameters -----------------------------------------------------------------------------

std::int32_t apiParamCount(magda_device* handle) {
    return handle != nullptr ? handle->parameterCount.load(std::memory_order_acquire) : 0;
}

std::int32_t apiParamOffered(magda_device* handle, std::int32_t slot) {
    if (handle == nullptr || !handle->validSlot(slot))
        return 0;
    ControlCall call(handle);
    return handle->device->offersParameter(slot) ? 1 : 0;
}

magda_status apiParamDescriptor(magda_device* handle, std::int32_t slot, char* buffer,
                                std::int32_t capacity, std::int32_t* size) {
    if (handle == nullptr)
        return MAGDA_ERR_ARGUMENT;
    ControlCall call(handle);
    if (!handle->validSlot(slot))
        return handle->fail(MAGDA_ERR_ARGUMENT, "no such slot");
    std::string error;
    const auto json =
        writeManifestParameter(resolveDescriptor(handle->device->parameterDescriptor(slot),
                                                 handle->document.deviceType, slot),
                               error);
    if (!json)
        return handle->fail(MAGDA_ERR_REJECTED, std::move(error));
    return handle->writeText(*json, buffer, capacity, size);
}

float apiParamValue(magda_device* handle, std::int32_t slot) {
    return handle != nullptr && handle->validSlot(slot) ? handle->device->parameterValue(slot)
                                                        : 0.0f;
}

float apiParamToReal(magda_device* handle, std::int32_t slot, float normalized) {
    if (handle == nullptr || !handle->validSlot(slot))
        return 0.0f;
    return normalizedToReal(normalized, handle->domains[static_cast<std::size_t>(slot)]);
}

float apiParamToNormalized(magda_device* handle, std::int32_t slot, float real) {
    if (handle == nullptr || !handle->validSlot(slot))
        return 0.0f;
    return realToNormalized(real, handle->domains[static_cast<std::size_t>(slot)]);
}

magda_status apiGetManifest(magda_device* handle, char* buffer, std::int32_t capacity,
                            std::int32_t* size) {
    if (handle == nullptr)
        return MAGDA_ERR_ARGUMENT;
    ControlCall call(handle);
    std::string error;
    const auto json = writeManifest(buildManifest(*handle->device), error);
    if (!json)
        return handle->fail(MAGDA_ERR_REJECTED, std::move(error));
    return handle->writeText(*json, buffer, capacity, size);
}

// ---- State ----------------------------------------------------------------------------------

magda_status apiGetState(magda_device* handle, char* buffer, std::int32_t capacity,
                         std::int32_t* size) {
    if (handle == nullptr)
        return MAGDA_ERR_ARGUMENT;
    ControlCall call(handle);
    if (handle->futureText)
        return handle->writeText(*handle->futureText, buffer, capacity, size);
    std::string error;
    const auto json = encodeDocument(handle->document, &error);
    if (!json)
        return handle->fail(MAGDA_ERR_REJECTED, std::move(error));
    return handle->writeText(*json, buffer, capacity, size);
}

magda_status apiSetState(magda_device* handle, const char* json, std::int32_t size) {
    if (handle == nullptr)
        return MAGDA_ERR_ARGUMENT;
    ControlCall call(handle, false);
    if (json == nullptr || size < 0)
        return handle->fail(MAGDA_ERR_ARGUMENT, "json is null or size negative");
    const std::string_view text(json, static_cast<std::size_t>(size));

    struct Guard {
        bool& flag;
        explicit Guard(bool& f) : flag(f) {
            flag = true;
        }
        ~Guard() {
            flag = false;
        }
    } guard(handle->inSetState);

    const auto adopted = [handle] {
        handle->pendingPatch.reset();
        handle->pending &= ~static_cast<std::uint32_t>(MAGDA_NOTIFY_STATE_CHANGED);
        handle->unannounced &= ~static_cast<std::uint32_t>(MAGDA_NOTIFY_STATE_CHANGED);
        if (handle->properties.parameterSource == ParameterSource::State) {
            handle->refreshParameters();
            handle->raise(MAGDA_NOTIFY_PARAMETERS_CHANGED);
        }
    };

    if (isFutureSchema(text)) {
        const auto restored = handle->device->restoreState(StateNode{});
        if (!restored.ok)
            return handle->fail(MAGDA_ERR_REJECTED, restored.message);
        handle->futureText = std::string(text);
        handle->document.root = {};
        adopted();
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
    if (!restored.ok)
        return handle->fail(MAGDA_ERR_REJECTED, restored.message);
    handle->document = std::move(*decoded.document);
    handle->futureText.reset();
    adopted();
    return MAGDA_OK;
}

// ---- Notifications --------------------------------------------------------------------------

std::uint32_t apiTakeNotifications(magda_device* handle) {
    if (handle == nullptr)
        return 0;
    ControlCall call(handle, false);
    handle->rebuildIfAsked();
    const auto flags = handle->pending;
    handle->pending = handle->unannounced = 0;
    return flags;
}

magda_status apiTakeStatePatch(magda_device* handle, char* buffer, std::int32_t capacity,
                               std::int32_t* size) {
    if (handle == nullptr)
        return MAGDA_ERR_ARGUMENT;
    ControlCall call(handle);
    if (!handle->pendingPatch)
        return handle->fail(MAGDA_ERR_STATE, "no state patch is pending");
    StateDocument patch;
    patch.deviceType = handle->document.deviceType;
    patch.root = *handle->pendingPatch;
    std::string error;
    const auto json = encodeDocument(patch, &error);
    if (!json)
        return handle->fail(MAGDA_ERR_REJECTED, std::move(error));
    const auto status = handle->writeText(*json, buffer, capacity, size);
    if (status == MAGDA_OK)
        handle->pendingPatch.reset();
    return status;
}

const char* apiLastError(magda_device* handle) {
    return handle != nullptr ? handle->error.c_str() : "";
}

const void* apiGetExtension(const char*) {
    return nullptr;
}

constexpr magda_device_api kApi{
    MAGDA_TAG_DEVICE_API,
    sizeof(magda_device_api),
    apiCreate,
    apiDestroy,
    apiGetProperties,
    apiPrepare,
    apiRelease,
    apiReset,
    apiLatency,
    apiTail,
    apiProcess,
    apiSetParam,
    apiSetParamSegments,
    apiParamCount,
    apiParamOffered,
    apiParamDescriptor,
    apiParamValue,
    apiParamToReal,
    apiParamToNormalized,
    apiGetManifest,
    apiGetState,
    apiSetState,
    apiTakeNotifications,
    apiTakeStatePatch,
    apiLastError,
};

}  // namespace

extern "C" const magda_module* magda_module_entry(std::int32_t host_abi_version) {
    if (host_abi_version != MAGDA_ABI_VERSION)
        return nullptr;
    static const std::string sdkVersion(versionString());
    static const magda_module module = [] {
        const auto& t = tables();
        return magda_module{MAGDA_TAG_MODULE,   sizeof(magda_module),
                            MAGDA_ABI_VERSION,  kStateSchemaVersion,
                            sdkVersion.c_str(), static_cast<std::int32_t>(t.types.size()),
                            t.types.data(),     &kApi,
                            apiGetExtension};
    }();
    return &module;
}
