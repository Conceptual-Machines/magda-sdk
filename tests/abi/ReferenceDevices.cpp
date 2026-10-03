// The reference module the ABI tests and the SDK's own parity run use.

#include <cmath>
#include <numbers>

#include "magda/sdk/abi/DeviceModule.hpp"
#include "magda/sdk/device/Analysis.hpp"

namespace {

using namespace magda::sdk;

ParameterDescriptor sineDescriptor(int slot) {
    ParameterDescriptor descriptor;
    descriptor.index = slot;
    if (slot == 0) {
        descriptor.stableId = "frequency";
        descriptor.name = "Frequency";
        descriptor.unit = "Hz";
        descriptor.scale = ParameterScale::Logarithmic;
        descriptor.minValue = 20.0f;
        descriptor.maxValue = 20000.0f;
        descriptor.defaultValue = 440.0f;
    } else {
        descriptor.stableId = "level";
        descriptor.name = "Level";
        descriptor.unit = "dB";
        descriptor.minValue = -60.0f;
        descriptor.maxValue = 0.0f;
        descriptor.defaultValue = -12.0f;
    }
    return descriptor;
}

/// A sine whose note-ons retune it; state "inverted" flips its polarity. Analyzes to RMS.
class ReferenceSine final : public Device, public AnalyzingDevice {
  public:
    DeviceProperties properties() const override {
        return {.pluginId = "sdkReferenceSine",
                .name = "Reference Sine",
                .takesMidiInput = true,
                .takesAudioInput = false,
                .isSynth = true,
                .producesAudioWithoutInput = true};
    }

    void prepare(const PrepareContext& context) override {
        sampleRate_ = context.sampleRate;
        phase_ = 0.0;
    }

    void reset() override {
        phase_ = 0.0;
    }

    void process(ProcessContext& context) override {
        const auto frames = context.numSamples();
        int nextEvent = 0;
        const int events = context.midiIn != nullptr ? context.midiIn->size() : 0;
        const float gain = std::pow(10.0f, levelDb() / 20.0f) * (inverted_ ? -1.0f : 1.0f);
        for (int i = 0; i < frames; ++i) {
            for (; nextEvent < events && context.midiIn->event(nextEvent).sample <= i; ++nextEvent)
                if (const auto& event = context.midiIn->event(nextEvent); event.isNoteOn())
                    noteHz_ = 440.0 * std::pow(2.0, (event.noteNumber() - 69) / 12.0);
            const double hz = noteHz_ > 0.0 ? noteHz_ : frequencyHz();
            const auto sample =
                static_cast<float>(std::sin(2.0 * std::numbers::pi * phase_)) * gain;
            for (int c = 0; c < context.audio.numChannels(); ++c)
                context.audio.channel(c)[i] = sample;
            phase_ += hz / sampleRate_;
            phase_ -= std::floor(phase_);
        }
    }

    int parameterCount() const override {
        return 2;
    }

    ParameterDescriptor parameterDescriptor(int slot) const override {
        return sineDescriptor(slot);
    }

    float parameterValue(int slot) const override {
        return values_[slot];
    }

    void setParameterValue(int slot, float normalized) override {
        values_[slot] = normalized;
    }

    RestoreResult restoreState(const StateNode& state) override {
        const auto* value = state.find("inverted");
        if (value != nullptr && value->kind() == StateValue::Kind::String)
            return RestoreResult::error("inverted is a bool");
        inverted_ = state.getBool("inverted", false);
        return RestoreResult::success();
    }

    AnalysisResult analyze(std::span<const float> mono, double) override {
        double sum = 0.0;
        for (const auto sample : mono)
            sum += static_cast<double>(sample) * sample;
        const auto rms = mono.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(mono.size()));
        return AnalysisResult::success("{\"rms\":" + std::to_string(rms) + "}");
    }

  private:
    double frequencyHz() const {
        return normalizedToReal(values_[0], domainOf(sineDescriptor(0)));
    }
    float levelDb() const {
        return normalizedToReal(values_[1], domainOf(sineDescriptor(1)));
    }

    float values_[2] = {realToNormalized(440.0f, domainOf(sineDescriptor(0))),
                        realToNormalized(-12.0f, domainOf(sineDescriptor(1)))};
    double sampleRate_ = 48000.0;
    double phase_ = 0.0;
    double noteHz_ = 0.0;
    bool inverted_ = false;
};

/// An effect: a gain on its input that echoes every MIDI message it is sent.
class ReferenceGain final : public Device {
  public:
    DeviceProperties properties() const override {
        return {.pluginId = "sdkReferenceGain",
                .name = "Reference Gain",
                .takesMidiInput = true,
                .producesMidi = true};
    }

    void process(ProcessContext& context) override {
        for (int c = 0; c < context.audio.numChannels(); ++c)
            for (int i = 0; i < context.numSamples(); ++i)
                context.audio.channel(c)[i] *= gain_;
        if (context.midiIn != nullptr)
            for (int e = 0; e < context.midiIn->size(); ++e)
                context.midiOut->addEvent(context.midiIn->event(e));
    }

    int parameterCount() const override {
        return 1;
    }
    ParameterDescriptor parameterDescriptor(int) const override {
        ParameterDescriptor descriptor;
        descriptor.stableId = "gain";
        descriptor.name = "Gain";
        descriptor.defaultValue = 0.5f;
        return descriptor;
    }
    float parameterValue(int) const override {
        return gain_;
    }
    void setParameterValue(int, float normalized) override {
        gain_ = normalized;
    }

  private:
    float gain_ = 0.5f;
};

constexpr magda::sdk::abi::DeviceFactory kDevices[] = {
    {"sdkReferenceSine",
     []() -> std::unique_ptr<Device> { return std::make_unique<ReferenceSine>(); }},
    {"sdkReferenceGain",
     []() -> std::unique_ptr<Device> { return std::make_unique<ReferenceGain>(); }},
};

}  // namespace

std::span<const magda::sdk::abi::DeviceFactory> magda::sdk::abi::moduleDevices() {
    return kDevices;
}
