// The smallest device module: one gain device behind the C ABI (docs/abi.md).

#include <atomic>
#include <cmath>

#include "magda/sdk/abi/DeviceModule.hpp"

namespace {

using namespace magda::sdk;

ParameterDescriptor gainDescriptor() {
    ParameterDescriptor descriptor;
    descriptor.stableId = "gain";
    descriptor.index = 0;
    descriptor.name = "Gain";
    descriptor.unit = "dB";
    descriptor.minValue = -60.0f;
    descriptor.maxValue = 12.0f;
    descriptor.defaultValue = 0.0f;
    return descriptor;
}

class ExampleGain final : public Device {
  public:
    DeviceProperties properties() const override {
        return {.pluginId = "exampleGain", .name = "Example Gain"};
    }

    void process(ProcessContext& context) override {
        const auto db = normalizedToReal(gain_.load(std::memory_order_relaxed), domain_);
        const auto factor = std::pow(10.0f, db / 20.0f);
        for (int c = 0; c < context.audio.numChannels(); ++c)
            for (int i = 0; i < context.numSamples(); ++i)
                context.audio.channel(c)[i] *= factor;
    }

    int parameterCount() const override {
        return 1;
    }
    ParameterDescriptor parameterDescriptor(int) const override {
        return gainDescriptor();
    }
    float parameterValue(int) const override {
        return gain_.load(std::memory_order_relaxed);
    }
    void setParameterValue(int, float normalized) override {
        gain_.store(normalized, std::memory_order_relaxed);
    }

  private:
    const ParameterDomain domain_ = domainOf(gainDescriptor());
    std::atomic<float> gain_{realToNormalized(0.0f, domain_)};
};

constexpr abi::DeviceFactory kDevices[] = {
    {"exampleGain", []() -> std::unique_ptr<Device> { return std::make_unique<ExampleGain>(); }},
};

}  // namespace

std::span<const magda::sdk::abi::DeviceFactory> magda::sdk::abi::moduleDevices() {
    return kDevices;
}
