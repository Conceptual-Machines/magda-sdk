#include "MagdaDeviceProcessor.hpp"

#include <atomic>
#include <limits>

namespace magda::sdk::juce_host {

namespace {

constexpr auto kPluginStateFormat = "magda.plugin-state";
constexpr int kPluginStateVersion = 1;

juce::String typeNameOf(const juce::String& deviceType) {
    if (const auto* module = host::linkedModule())
        for (int i = 0; i < module->device_type_count; ++i)
            if (deviceType == module->device_types[i].id)
                return juce::String::fromUTF8(module->device_types[i].name);
    return deviceType;
}

}  // namespace

/**
 * @brief One parameter slot. Values are normalized; text goes through the ABI's reference
 * conversion, a control call, so under the callback lock.
 */
class MagdaDeviceProcessor::Parameter final : public juce::AudioProcessorParameterWithID {
  public:
    Parameter(host::AbiDevice& device, const juce::CriticalSection& controlLock, int slot,
              const juce::var& descriptor)
        : AudioProcessorParameterWithID(juce::ParameterID(descriptor["id"].toString(), 1),
                                        descriptor["name"].toString()),
          device_(device),
          controlLock_(controlLock),
          slot_(slot),
          default_(device.api().param_value(device.get(), slot)),
          value_(default_) {
        describe(descriptor);
    }

    int slot() const {
        return slot_;
    }

    /// Message thread, under the callback lock: re-reads the slot after a parameters change.
    void refresh() {
        const auto text = device_.parameterDescriptor(slot_);
        describe(text.empty() ? juce::var()
                              : juce::JSON::parse(juce::String::fromUTF8(text.c_str())));
        value_.store(device_.api().param_value(device_.get(), slot_), std::memory_order_relaxed);
    }

    float getValue() const override {
        return value_.load(std::memory_order_relaxed);
    }
    void setValue(float newValue) override {
        value_.store(newValue, std::memory_order_relaxed);
    }
    float getDefaultValue() const override {
        return default_;
    }

    juce::String getName(int maximumLength) const override {
        const juce::SpinLock::ScopedLockType lock(descriptorLock_);
        return name_.substring(0, maximumLength);
    }
    juce::String getLabel() const override {
        const juce::SpinLock::ScopedLockType lock(descriptorLock_);
        return unit_;
    }

    bool isDiscrete() const override {
        return isChoice() || isBoolean();
    }
    bool isBoolean() const override {
        const juce::SpinLock::ScopedLockType lock(descriptorLock_);
        return kind_ == "boolean";
    }
    int getNumSteps() const override {
        if (isBoolean())
            return 2;
        if (isChoice())
            return choices().size();
        return AudioProcessorParameter::getNumSteps();
    }
    juce::StringArray getAllValueStrings() const override {
        return isChoice() ? choices() : juce::StringArray{};
    }

    juce::String getText(float normalized, int maximumLength) const override {
        if (!inUse())
            return {};
        float real = 0.0f;
        {
            const juce::ScopedLock lock(controlLock_);
            real = device_.api().param_to_real(device_.get(), slot_, normalized);
        }
        juce::String text;
        if (isBoolean())
            text = real >= 0.5f ? "On" : "Off";
        else if (const auto labels = choices(); isChoice())
            text = labels[juce::jlimit(0, labels.size() - 1, juce::roundToInt(real))];
        else
            text = juce::String(real, 2);
        return text.substring(0, maximumLength);
    }

    float getValueForText(const juce::String& text) const override {
        if (isBoolean())
            return text.equalsIgnoreCase("on") || text.getIntValue() != 0 ? 1.0f : 0.0f;
        const auto real = isChoice()
                              ? static_cast<float>(juce::jmax(0, choices().indexOf(text, true)))
                              : text.getFloatValue();
        const juce::ScopedLock lock(controlLock_);
        return device_.api().param_to_normalized(device_.get(), slot_, real);
    }

  private:
    /// A null descriptor is a slot past the instance's count: kept registered, out of use.
    void describe(const juce::var& descriptor) {
        juce::StringArray labels;
        if (const auto* list = descriptor["scale"]["choices"].getArray())
            for (const auto& choice : *list)
                labels.add(choice["label"].toString());

        const juce::SpinLock::ScopedLockType lock(descriptorLock_);
        inUse_ = descriptor.isObject();
        name_ = inUse_ ? descriptor["name"].toString() : juce::String("Unused");
        unit_ = descriptor["unit"].toString();
        kind_ = descriptor["scale"]["kind"].toString();
        choices_ = std::move(labels);
    }

    bool inUse() const {
        const juce::SpinLock::ScopedLockType lock(descriptorLock_);
        return inUse_;
    }
    juce::StringArray choices() const {
        const juce::SpinLock::ScopedLockType lock(descriptorLock_);
        return choices_;
    }
    bool isChoice() const {
        const juce::SpinLock::ScopedLockType lock(descriptorLock_);
        return kind_ == "discrete" && choices_.size() > 1;
    }

    host::AbiDevice& device_;
    const juce::CriticalSection& controlLock_;
    int slot_;
    float default_;
    std::atomic<float> value_;

    juce::SpinLock descriptorLock_;
    bool inUse_ = true;
    juce::String name_;
    juce::String unit_;
    juce::String kind_;
    juce::StringArray choices_;
};

MagdaDeviceProcessor::MagdaDeviceProcessor(const char* deviceType, Layout layout)
    : AudioProcessor([&layout] {
          BusesProperties buses;
          if (layout.audioInput)
              buses = buses.withInput("Input", juce::AudioChannelSet::stereo(), true);
          return buses.withOutput("Output", juce::AudioChannelSet::stereo(), true);
      }()),
      deviceType_(deviceType),
      deviceName_(typeNameOf(deviceType_)),
      layout_(layout) {
    host_.struct_tag = MAGDA_TAG_HOST;
    host_.struct_size = sizeof(host_);
    host_.context = this;
    host_.notify = [](void* context, magda_device*) {
        static_cast<MagdaDeviceProcessor*>(context)->triggerAsyncUpdate();
    };
    device_ = std::make_unique<host::AbiDevice>(*host::linkedModule(), deviceType, &host_);
    if (!hasDevice())
        return;

    for (int slot = 0; slot < device_->parameterCount(); ++slot) {
        const auto descriptor =
            juce::JSON::parse(juce::String::fromUTF8(device_->parameterDescriptor(slot).c_str()));
        auto parameter = std::make_unique<Parameter>(*device_, getCallbackLock(), slot, descriptor);
        parameters_.push_back(parameter.get());
        addParameter(parameter.release());
    }
    applied_.assign(parameters_.size(), -1.0f);
}

MagdaDeviceProcessor::~MagdaDeviceProcessor() {
    device_.reset();
    cancelPendingUpdate();
}

const juce::String MagdaDeviceProcessor::getName() const {
#ifdef JucePlugin_Name
    return JucePlugin_Name;
#else
    return deviceName_;
#endif
}

bool MagdaDeviceProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& output = layouts.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;
    return !layout_.audioInput || layouts.getMainInputChannelSet() == output;
}

void MagdaDeviceProcessor::prepareToPlay(double sampleRate, int maximumBlockSize) {
    if (!hasDevice())
        return;
    {
        const juce::ScopedLock lock(getCallbackLock());
        device_->prepare(sampleRate, maximumBlockSize);
        sampleRate_.store(sampleRate, std::memory_order_relaxed);
        std::fill(applied_.begin(), applied_.end(), -1.0f);
    }
    applyNotifications();
    setLatencySamples(device_->api().latency(device_->get()));
}

void MagdaDeviceProcessor::releaseResources() {
    if (!hasDevice())
        return;
    const juce::ScopedLock lock(getCallbackLock());
    device_->api().release(device_->get());
}

void MagdaDeviceProcessor::reset() {
    if (!hasDevice())
        return;
    const juce::ScopedLock lock(getCallbackLock());
    device_->api().reset(device_->get());
}

double MagdaDeviceProcessor::getTailLengthSeconds() const {
    const auto sampleRate = sampleRate_.load(std::memory_order_relaxed);
    if (!hasDevice() || sampleRate <= 0.0)
        return 0.0;
    const auto tail = device_->api().tail(device_->get());
    if (tail < 0)
        return std::numeric_limits<double>::infinity();
    return static_cast<double>(tail) / sampleRate;
}

magda_transport MagdaDeviceProcessor::transportFor(int numFrames) const {
    magda_transport transport{};
    transport.struct_tag = MAGDA_TAG_TRANSPORT;
    transport.struct_size = sizeof(transport);
    juce::Optional<juce::AudioPlayHead::PositionInfo> position;
    if (const auto* playHead = getPlayHead())
        position = playHead->getPosition();
    const auto sampleRate = sampleRate_.load(std::memory_order_relaxed);
    if (!position || sampleRate <= 0.0)
        return transport;

    if (const auto seconds = position->getTimeInSeconds())
        transport.block_start_seconds = *seconds;
    else if (const auto samples = position->getTimeInSamples())
        transport.block_start_seconds = static_cast<double>(*samples) / sampleRate;
    transport.block_end_seconds = transport.block_start_seconds + numFrames / sampleRate;
    if (position->getIsPlaying())
        transport.flags |= MAGDA_TRANSPORT_PLAYING;
    if (isNonRealtime())
        transport.flags |= MAGDA_TRANSPORT_RENDERING;
    if (const auto bpm = position->getBpm(); bpm && *bpm > 0.0) {
        transport.tempo_kind = MAGDA_TEMPO_CONSTANT;
        transport.bpm = *bpm;
    }
    return transport;
}

void MagdaDeviceProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    for (int channel = getTotalNumInputChannels(); channel < buffer.getNumChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
    if (!hasDevice()) {
        buffer.clear();
        return;
    }

    for (std::size_t i = 0; i < parameters_.size(); ++i) {
        const auto value = parameters_[i]->getValue();
        if (value != applied_[i]) {
            device_->api().set_param(device_->get(), parameters_[i]->slot(), value);
            applied_[i] = value;
        }
    }

    if (layout_.midiInput)
        for (const auto metadata : midi)
            device_->queueMidi(metadata.data, metadata.numBytes, metadata.samplePosition);

    const auto transport = transportFor(buffer.getNumSamples());
    device_->process(buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                     buffer.getNumSamples(), getPlayHead() != nullptr ? &transport : nullptr);

    midi.clear();
    if (layout_.midiOutput) {
        for (int i = 0; i < device_->midiOutCount(); ++i) {
            const auto& event = device_->midiOut(i);
            midi.addEvent(host::AbiDevice::bytesOf(event), static_cast<int>(event.size),
                          event.sample);
        }
    }
}

void MagdaDeviceProcessor::applyNotifications() {
    if (!hasDevice())
        return;

    std::uint32_t flags = 0;
    int latency = 0;
    {
        const juce::ScopedLock lock(getCallbackLock());
        flags = device_->api().take_notifications(device_->get());
        if ((flags & MAGDA_NOTIFY_PARAMETERS_CHANGED) != 0)
            for (auto* parameter : parameters_)
                parameter->refresh();
        if ((flags & MAGDA_NOTIFY_STATE_CHANGED) != 0)
            device_->takeStatePatch();
        latency = device_->api().latency(device_->get());
    }

    if ((flags & MAGDA_NOTIFY_PROPERTIES_CHANGED) != 0)
        setLatencySamples(latency);
    auto details = ChangeDetails{}
                       .withParameterInfoChanged((flags & MAGDA_NOTIFY_PARAMETERS_CHANGED) != 0)
                       .withNonParameterStateChanged((flags & MAGDA_NOTIFY_STATE_CHANGED) != 0);
    if (details.parameterInfoChanged || details.nonParameterStateChanged)
        updateHostDisplay(details);
}

void MagdaDeviceProcessor::handleAsyncUpdate() {
    applyNotifications();
}

juce::AudioProcessorEditor* MagdaDeviceProcessor::createEditor() {
    return new juce::GenericAudioProcessorEditor(*this);
}

void MagdaDeviceProcessor::getStateInformation(juce::MemoryBlock& destination) {
    if (!hasDevice())
        return;

    auto* parameters = new juce::DynamicObject;
    for (auto* parameter : parameters_)
        parameters->setProperty(parameter->getParameterID(), parameter->getValue());

    std::string deviceState;
    {
        const juce::ScopedLock lock(getCallbackLock());
        deviceState = device_->state();
    }

    // The device document goes in verbatim, so a newer schema's text survives the round trip.
    const auto text =
        "{\n  \"format\": \"" + std::string(kPluginStateFormat) +
        "\",\n  \"version\": " + std::to_string(kPluginStateVersion) +
        ",\n  \"parameters\": " + juce::JSON::toString(juce::var(parameters), true).toStdString() +
        ",\n  \"state\": " + deviceState + "\n}";
    destination.replaceAll(text.data(), text.size());
}

void MagdaDeviceProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (!hasDevice())
        return;

    const auto root =
        juce::JSON::parse(juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes));
    if (root["format"].toString() != kPluginStateFormat)
        return;

    if (const auto state = root["state"]; state.isObject()) {
        const auto text = juce::JSON::toString(state, true).toStdString();
        const juce::ScopedLock lock(getCallbackLock());
        device_->setState(text);
    }
    applyNotifications();

    if (const auto* values = root["parameters"].getDynamicObject())
        for (auto* parameter : parameters_)
            if (const auto* value =
                    values->getProperties().getVarPointer(parameter->getParameterID()))
                parameter->setValueNotifyingHost(static_cast<float>(*value));
}

}  // namespace magda::sdk::juce_host

#ifdef MAGDA_SDK_PLUGIN_DEVICE_TYPE
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new magda::sdk::juce_host::MagdaDeviceProcessor(
        MAGDA_SDK_PLUGIN_DEVICE_TYPE, {.audioInput = !JucePlugin_IsSynth,
                                       .midiInput = JucePlugin_WantsMidiInput != 0,
                                       .midiOutput = JucePlugin_ProducesMidiOutput != 0});
}
#endif
