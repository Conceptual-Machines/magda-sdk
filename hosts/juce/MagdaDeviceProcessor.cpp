#include "MagdaDeviceProcessor.hpp"

#include <atomic>

namespace magda::sdk::juce_host {

namespace {

constexpr auto kPluginStateFormat = "magda.plugin-state";
constexpr int kPluginStateVersion = 1;

}  // namespace

/// One manifest parameter. Values are normalized; text goes through the ABI's reference conversion.
class MagdaDeviceProcessor::Parameter final : public juce::AudioProcessorParameterWithID {
  public:
    Parameter(host::AbiDevice& device, int slot, const juce::var& manifest)
        : AudioProcessorParameterWithID(juce::ParameterID(manifest["id"].toString(), 1),
                                        manifest["name"].toString()),
          device_(device),
          slot_(slot),
          unit_(manifest["unit"].toString()),
          kind_(manifest["scale"]["kind"].toString()),
          default_(device.api().param_value(device.get(), slot)),
          value_(default_) {
        if (const auto* choices = manifest["scale"]["choices"].getArray())
            for (const auto& choice : *choices)
                choices_.add(choice["label"].toString());
    }

    int slot() const {
        return slot_;
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

    juce::String getLabel() const override {
        return unit_;
    }

    bool isDiscrete() const override {
        return isChoice() || isBoolean();
    }
    bool isBoolean() const override {
        return kind_ == "boolean";
    }
    int getNumSteps() const override {
        if (isBoolean())
            return 2;
        if (isChoice())
            return choices_.size();
        return AudioProcessorParameter::getNumSteps();
    }
    juce::StringArray getAllValueStrings() const override {
        return isChoice() ? choices_ : juce::StringArray{};
    }

    juce::String getText(float normalized, int maximumLength) const override {
        const auto real = device_.api().param_to_real(device_.get(), slot_, normalized);
        juce::String text;
        if (isBoolean())
            text = real >= 0.5f ? "On" : "Off";
        else if (isChoice())
            text = choices_[juce::jlimit(0, choices_.size() - 1, juce::roundToInt(real))];
        else
            text = juce::String(real, 2);
        return text.substring(0, maximumLength);
    }

    float getValueForText(const juce::String& text) const override {
        if (isBoolean())
            return text.equalsIgnoreCase("on") || text.getIntValue() != 0 ? 1.0f : 0.0f;
        if (isChoice()) {
            const auto index = choices_.indexOf(text, true);
            return device_.api().param_to_normalized(device_.get(), slot_,
                                                     static_cast<float>(juce::jmax(0, index)));
        }
        return device_.api().param_to_normalized(device_.get(), slot_, text.getFloatValue());
    }

  private:
    bool isChoice() const {
        return kind_ == "discrete" && choices_.size() > 1;
    }

    host::AbiDevice& device_;
    int slot_;
    juce::String unit_;
    juce::String kind_;
    juce::StringArray choices_;
    float default_;
    std::atomic<float> value_;
};

MagdaDeviceProcessor::MagdaDeviceProcessor(const char* deviceType, Layout layout)
    : AudioProcessor([&layout] {
          BusesProperties buses;
          if (layout.audioInput)
              buses = buses.withInput("Input", juce::AudioChannelSet::stereo(), true);
          return buses.withOutput("Output", juce::AudioChannelSet::stereo(), true);
      }()),
      deviceType_(deviceType),
      layout_(layout),
      device_(std::make_unique<host::AbiDevice>(*host::linkedModule(), deviceType)) {
    if (!hasDevice())
        return;

    const auto manifest = juce::JSON::parse(juce::String::fromUTF8(device_->manifest().c_str()));
    if (const auto* parameters = manifest["parameters"].getArray()) {
        for (const auto& description : *parameters) {
            auto parameter = std::make_unique<Parameter>(
                *device_, static_cast<int>(description["index"]), description);
            parameters_.push_back(parameter.get());
            addParameter(parameter.release());
        }
    }
    applied_.assign(parameters_.size(), -1.0f);
}

MagdaDeviceProcessor::~MagdaDeviceProcessor() = default;

const juce::String MagdaDeviceProcessor::getName() const {
    return JucePlugin_Name;
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
    device_->prepare(sampleRate, maximumBlockSize);
    std::fill(applied_.begin(), applied_.end(), -1.0f);
    setLatencySamples(device_->api().latency(device_->get()));
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

    device_->process(buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                     buffer.getNumSamples());

    midi.clear();
    if (layout_.midiOutput) {
        for (int i = 0; i < device_->midiOutCount(); ++i) {
            const auto& event = device_->midiOut(i);
            midi.addEvent(host::AbiDevice::bytesOf(event), static_cast<int>(event.size),
                          event.sample);
        }
    }
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

    juce::var deviceState;
    {
        const juce::ScopedLock lock(getCallbackLock());
        deviceState = juce::JSON::parse(juce::String::fromUTF8(device_->state().c_str()));
    }

    auto* root = new juce::DynamicObject;
    root->setProperty("format", kPluginStateFormat);
    root->setProperty("version", kPluginStateVersion);
    root->setProperty("parameters", juce::var(parameters));
    root->setProperty("state", deviceState);
    const auto text = juce::JSON::toString(juce::var(root), true).toStdString();
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
