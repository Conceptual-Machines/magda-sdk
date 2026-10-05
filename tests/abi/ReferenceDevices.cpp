// The reference module the ABI tests and the SDK's own parity run use.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

#include "magda/sdk/abi/DeviceModule.hpp"

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

/// A sine whose note-ons retune it; state "inverted" flips its polarity.
class ReferenceSine final : public Device {
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

/**
 * @brief Reports what the ABI hands it: channel 0 holds the beat at the block start (-1 without a
 * tempo), channel 1 the sidechain key (-2 without one). State "slots" sets its parameter count,
 * "latency" its latency (asking for a rebuild); each reset reports a "resets" count as state.
 */
class ReferenceProbe final : public Device {
  public:
    void setHost(DeviceHost* host) override {
        host_ = host;
    }

    DeviceProperties properties() const override {
        return {.pluginId = "sdkReferenceProbe",
                .name = "Reference Probe",
                .parameterSource = ParameterSource::State,
                .sidechain = monoAudioSidechain,
                .outputChannelCount = 2};
    }

    int latencySamples() const override {
        return latency_;
    }
    std::int64_t tailSamples() const override {
        return kInfiniteTail;
    }

    void reset() override {
        StateNode report;
        report.setInt("resets", ++resets_);
        if (host_ != nullptr)
            host_->stateChanged(std::move(report));
    }

    void process(ProcessContext& context) override {
        const double beat = context.tempoMap != nullptr
                                ? context.tempoMap->beatsAtSeconds(context.timelineStartSeconds)
                                : -1.0;
        for (int i = 0; i < context.numSamples(); ++i) {
            if (context.audio.numChannels() > 0)
                context.audio.channel(0)[i] = static_cast<float>(beat);
            if (context.audio.numChannels() > 1)
                context.audio.channel(1)[i] =
                    context.sidechain && context.sidechain->numChannels() > 0
                        ? context.sidechain->channel(0)[i]
                        : -2.0f;
        }
    }

    int parameterCount() const override {
        return slots_;
    }
    ParameterDescriptor parameterDescriptor(int slot) const override {
        ParameterDescriptor descriptor;
        descriptor.stableId = "slot_" + std::to_string(slot);
        descriptor.name = "Slot " + std::to_string(slot);
        return descriptor;
    }

    RestoreResult restoreState(const StateNode& state) override {
        slots_ = std::clamp(state.getInt("slots", 1), 0, 8);
        const int latency = std::max(0, state.getInt("latency", 0));
        if (latency != latency_) {
            latency_ = latency;
            if (host_ != nullptr)
                host_->rebuildRequired();
        }
        return RestoreResult::success();
    }

  private:
    DeviceHost* host_ = nullptr;
    int slots_ = 1;
    int latency_ = 0;
    int resets_ = 0;
};

constexpr magda::sdk::abi::DeviceFactory kDevices[] = {
    {"sdkReferenceSine",
     []() -> std::unique_ptr<Device> { return std::make_unique<ReferenceSine>(); }},
    {"sdkReferenceGain",
     []() -> std::unique_ptr<Device> { return std::make_unique<ReferenceGain>(); }},
    {"sdkReferenceProbe",
     []() -> std::unique_ptr<Device> { return std::make_unique<ReferenceProbe>(); }},
};

}  // namespace

std::span<const magda::sdk::abi::DeviceFactory> magda::sdk::abi::moduleDevices() {
    return kDevices;
}
