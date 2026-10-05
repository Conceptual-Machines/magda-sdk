#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "MagdaDeviceProcessor.hpp"

using magda::sdk::juce_host::MagdaDeviceProcessor;

namespace {

constexpr MagdaDeviceProcessor::Layout kEffect{
    .audioInput = true, .midiInput = true, .midiOutput = true};

struct FixedPlayHead final : juce::AudioPlayHead {
    juce::Optional<PositionInfo> getPosition() const override {
        return position;
    }
    PositionInfo position;
};

/// Counts the host display changes the processor reports.
struct ChangeCounter final : juce::AudioProcessorListener {
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override {
        parameterInfo += details.parameterInfoChanged ? 1 : 0;
        nonParameterState += details.nonParameterStateChanged ? 1 : 0;
    }
    int parameterInfo = 0;
    int nonParameterState = 0;
};

juce::var pluginState(juce::AudioProcessor& processor) {
    juce::MemoryBlock block;
    processor.getStateInformation(block);
    return juce::JSON::parse(block.toString());
}

void setPluginState(juce::AudioProcessor& processor, const juce::String& deviceState) {
    const auto text =
        R"({"format": "magda.plugin-state", "version": 1, "state": )" + deviceState + "}";
    const auto utf8 = text.toStdString();
    processor.setStateInformation(utf8.data(), static_cast<int>(utf8.size()));
}

/// The last sample of @p channel after one block of @p input.
float render(MagdaDeviceProcessor& processor, int channel, float input = 0.0f) {
    juce::AudioBuffer<float> buffer(2, 64);
    for (int c = 0; c < 2; ++c)
        juce::FloatVectorOperations::fill(buffer.getWritePointer(c), input, 64);
    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
    return buffer.getSample(channel, 63);
}

}  // namespace

TEST_CASE("Parameters are the instance's slots, with text through the reference conversion",
          "[juce-host]") {
    const juce::ScopedJuceInitialiser_GUI juce;
    MagdaDeviceProcessor processor("sdkReferenceGain", kEffect);
    REQUIRE(processor.hasDevice());
    CHECK(processor.getName() == "Reference Gain");

    const auto& parameters = processor.getParameters();
    REQUIRE(parameters.size() == 1);
    CHECK(parameters[0]->getName(32) == "Gain");
    CHECK(parameters[0]->getDefaultValue() == Catch::Approx(0.5f));
    CHECK(parameters[0]->getText(0.25f, 16) == "0.25");

    processor.prepareToPlay(48000.0, 64);
    parameters[0]->setValue(0.25f);
    CHECK(render(processor, 0, 1.0f) == Catch::Approx(0.25f));
}

TEST_CASE("MIDI goes through the device and back out", "[juce-host]") {
    const juce::ScopedJuceInitialiser_GUI juce;
    MagdaDeviceProcessor processor("sdkReferenceGain", kEffect);
    processor.prepareToPlay(48000.0, 64);

    juce::AudioBuffer<float> buffer(2, 64);
    buffer.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.5f), 10);
    const std::uint8_t sysex[] = {0x7d, 1, 2, 3, 4};
    midi.addEvent(juce::MidiMessage::createSysExMessage(sysex, sizeof(sysex)), 20);
    processor.processBlock(buffer, midi);

    std::vector<std::pair<int, juce::MidiMessage>> echoed;
    for (const auto metadata : midi)
        echoed.emplace_back(metadata.samplePosition, metadata.getMessage());
    REQUIRE(echoed.size() == 2);
    CHECK(echoed[0].first == 10);
    CHECK(echoed[0].second.isNoteOn());
    CHECK(echoed[1].first == 20);
    CHECK(echoed[1].second.isSysEx());
    CHECK(echoed[1].second.getSysExDataSize() == static_cast<int>(sizeof(sysex)));
}

TEST_CASE("A state-sourced parameter set refreshes the registered slots", "[juce-host]") {
    const juce::ScopedJuceInitialiser_GUI juce;
    MagdaDeviceProcessor processor("sdkReferenceProbe", kEffect);
    ChangeCounter changes;
    processor.addListener(&changes);
    processor.prepareToPlay(48000.0, 64);

    const auto& parameters = processor.getParameters();
    REQUIRE(parameters.size() == 1);
    CHECK(parameters[0]->getName(32) == "Slot 0");

    setPluginState(processor,
                   R"({"schema": 2, "device": "sdkReferenceProbe", "props": {"slots": 0}})");
    CHECK(parameters[0]->getName(32) == "Unused");
    CHECK(parameters[0]->getText(0.5f, 16).isEmpty());
    CHECK(changes.parameterInfo == 1);

    setPluginState(processor,
                   R"({"schema": 2, "device": "sdkReferenceProbe", "props": {"slots": 4}})");
    CHECK(processor.getParameters().size() == 1);
    CHECK(parameters[0]->getName(32) == "Slot 0");
    CHECK(changes.parameterInfo == 2);
    processor.removeListener(&changes);
}

TEST_CASE("A rebuild the state asks for reaches the host's latency", "[juce-host]") {
    const juce::ScopedJuceInitialiser_GUI juce;
    MagdaDeviceProcessor processor("sdkReferenceProbe", kEffect);
    processor.prepareToPlay(48000.0, 64);
    CHECK(processor.getLatencySamples() == 0);
    CHECK(std::isinf(processor.getTailLengthSeconds()));

    setPluginState(processor,
                   R"({"schema": 2, "device": "sdkReferenceProbe", "props": {"latency": 32}})");
    CHECK(processor.getLatencySamples() == 32);

    processor.prepareToPlay(96000.0, 128);
    CHECK(processor.getLatencySamples() == 32);
}

TEST_CASE("The play head becomes the ABI transport", "[juce-host]") {
    const juce::ScopedJuceInitialiser_GUI juce;
    MagdaDeviceProcessor processor("sdkReferenceProbe", kEffect);
    processor.prepareToPlay(48000.0, 64);
    CHECK(render(processor, 0) == Catch::Approx(-1.0f));

    FixedPlayHead playHead;
    playHead.position.setTimeInSeconds(2.0);
    playHead.position.setBpm(120.0);
    playHead.position.setIsPlaying(true);
    processor.setPlayHead(&playHead);
    CHECK(render(processor, 0) == Catch::Approx(4.0f));

    playHead.position.setBpm({});
    CHECK(render(processor, 0) == Catch::Approx(-1.0f));
    processor.setPlayHead(nullptr);
}

TEST_CASE("Plugin state carries parameters and the device document", "[juce-host]") {
    const juce::ScopedJuceInitialiser_GUI juce;
    MagdaDeviceProcessor source("sdkReferenceGain", kEffect);
    source.getParameters()[0]->setValue(0.75f);
    const auto saved = pluginState(source);
    CHECK(saved["format"] == "magda.plugin-state");
    CHECK(saved["state"]["device"] == "sdkReferenceGain");
    CHECK(static_cast<float>(saved["parameters"]["gain"]) == Catch::Approx(0.75f));

    MagdaDeviceProcessor restored("sdkReferenceGain", kEffect);
    const auto text = juce::JSON::toString(saved).toStdString();
    restored.setStateInformation(text.data(), static_cast<int>(text.size()));
    CHECK(restored.getParameters()[0]->getValue() == Catch::Approx(0.75f));
}

TEST_CASE("A newer schema's document survives the plugin state round trip", "[juce-host]") {
    const juce::ScopedJuceInitialiser_GUI juce;
    MagdaDeviceProcessor processor("sdkReferenceProbe", kEffect);
    setPluginState(processor,
                   R"({"schema": 99, "device": "sdkReferenceProbe", "future": {"x": 1}})");
    const auto saved = pluginState(processor);
    CHECK(static_cast<int>(saved["state"]["schema"]) == 99);
    CHECK(static_cast<int>(saved["state"]["future"]["x"]) == 1);
}

TEST_CASE("State the device reports reaches the host through the notify callback", "[juce-host]") {
    const juce::ScopedJuceInitialiser_GUI juce;
    MagdaDeviceProcessor processor("sdkReferenceProbe", kEffect);
    ChangeCounter changes;
    processor.addListener(&changes);
    processor.prepareToPlay(48000.0, 64);

    processor.reset();
    // The callback posts an async update; dispatch until it lands, bounded.
    for (int i = 0; i < 100 && changes.nonParameterState == 0; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    CHECK(changes.nonParameterState == 1);
    CHECK(pluginState(processor)["state"]["props"]["resets"] == juce::var(1));
    processor.removeListener(&changes);
}
