#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "magda/sdk/abi/magda_device.h"

namespace magda::sdk::juce_host {

/**
 * @brief A juce::AudioProcessor over one device of the C ABI (docs/abi.md).
 *
 * Parameters come from the device's manifest; plugin state is the magda.plugin-state document the
 * WAM host also writes.
 */
class MagdaDeviceProcessor : public juce::AudioProcessor {
  public:
    struct Layout {
        bool audioInput = true;
        bool midiInput = false;
        bool midiOutput = false;
    };

    MagdaDeviceProcessor(const char* deviceType, Layout layout);
    ~MagdaDeviceProcessor() override;

    /// False when the module has no such device; the processor then outputs silence.
    bool hasDevice() const {
        return device_ != nullptr;
    }

    const juce::String getName() const override;
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override {
        return true;
    }

    bool acceptsMidi() const override {
        return layout_.midiInput;
    }
    bool producesMidi() const override {
        return layout_.midiOutput;
    }
    double getTailLengthSeconds() const override {
        return 0.0;
    }

    int getNumPrograms() override {
        return 1;
    }
    int getCurrentProgram() override {
        return 0;
    }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {
        return {};
    }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

  private:
    class Parameter;

    juce::String deviceType_;
    Layout layout_;
    magda_device* device_ = nullptr;
    std::vector<Parameter*> parameters_;
    std::vector<float> applied_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaDeviceProcessor)
};

}  // namespace magda::sdk::juce_host
