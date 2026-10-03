// Native side of the parity harness (docs/parity.md): renders a corpus through the C ABI and
// writes each case as planar little-endian float32 to <out>/<case>.f32.

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

#include "magda/sdk/abi/magda_device.h"
#include "magda/sdk/state/detail/Json.hpp"

namespace {

using magda::sdk::detail::JsonValue;

struct MidiAt {
    int sample = 0;
    std::vector<std::uint8_t> bytes;
};

double number(const JsonValue* value, double fallback) {
    if (value == nullptr)
        return fallback;
    if (value->type == JsonValue::Type::Int)
        return static_cast<double>(value->integer);
    if (value->type == JsonValue::Type::Double)
        return value->real;
    return fallback;
}

std::string text(const JsonValue* value, std::string fallback = {}) {
    return value != nullptr && value->type == JsonValue::Type::String ? value->string : fallback;
}

/// The input signal a case names; the same formula as parity.mjs.
float inputSample(const std::string& kind, double frequency, int frame, double sampleRate) {
    if (kind == "impulse")
        return frame == 0 ? 1.0f : 0.0f;
    if (kind == "sine")
        return static_cast<float>(
            0.5 * std::sin(2.0 * std::numbers::pi * frequency * frame / sampleRate));
    return 0.0f;
}

std::map<std::string, int> slotsById(magda_device* device) {
    std::map<std::string, int> slots;
    const char* manifestText = magda_device_get_manifest(device);
    if (manifestText == nullptr)
        return slots;
    std::string error;
    const auto manifest = magda::sdk::detail::parseJson(manifestText, error);
    if (!manifest)
        return slots;
    if (const auto* parameters = manifest->member("parameters"))
        for (const auto& parameter : parameters->array)
            slots[text(parameter.member("id"))] =
                static_cast<int>(number(parameter.member("index"), -1));
    return slots;
}

bool renderCase(const JsonValue& spec, const std::string& outDir) {
    const auto name = text(spec.member("name"));
    const auto type = text(spec.member("device"));
    const double sampleRate = number(spec.member("sampleRate"), 48000.0);
    const int blockSize = static_cast<int>(number(spec.member("blockSize"), 128));
    const int channels = static_cast<int>(number(spec.member("channels"), 2));
    const int frames = static_cast<int>(number(spec.member("frames"), sampleRate));

    auto* device = magda_device_create(type.c_str());
    if (device == nullptr) {
        std::cerr << name << ": unknown device " << type << "\n";
        return false;
    }

    if (const auto* state = spec.member("state")) {
        std::string json, error;
        magda::sdk::detail::appendJson(json, *state, -1, error);
        if (magda_device_set_state(device, json.c_str(), static_cast<int>(json.size())) != MAGDA_OK)
            std::cerr << name << ": state: " << magda_device_last_error(device) << "\n";
    }

    if (magda_device_prepare(device, sampleRate, blockSize) != MAGDA_OK) {
        magda_device_destroy(device);
        return false;
    }

    const auto slots = slotsById(device);
    if (const auto* parameters = spec.member("parameters"))
        for (const auto& [id, value] : parameters->object)
            if (const auto slot = slots.find(id); slot != slots.end())
                magda_device_set_param(device, slot->second, static_cast<float>(number(&value, 0)));

    std::vector<MidiAt> midi;
    if (const auto* events = spec.member("midi"))
        for (const auto& event : events->array) {
            MidiAt at{static_cast<int>(number(event.member("sample"), 0)), {}};
            if (const auto* bytes = event.member("bytes"))
                for (const auto& byte : bytes->array)
                    at.bytes.push_back(static_cast<std::uint8_t>(number(&byte, 0)));
            midi.push_back(std::move(at));
        }

    const auto inputKind = text(spec.member("input"), "silence");
    const double inputFrequency = number(spec.member("inputFrequency"), 440.0);

    std::vector<std::vector<float>> output(static_cast<std::size_t>(channels),
                                           std::vector<float>(static_cast<std::size_t>(frames)));
    std::vector<float*> pointers(static_cast<std::size_t>(channels));

    for (int start = 0; start < frames; start += blockSize) {
        const int count = std::min(blockSize, frames - start);
        for (int c = 0; c < channels; ++c) {
            auto& channel = output[static_cast<std::size_t>(c)];
            for (int i = 0; i < count; ++i)
                channel[static_cast<std::size_t>(start + i)] =
                    inputSample(inputKind, inputFrequency, start + i, sampleRate);
            pointers[static_cast<std::size_t>(c)] = channel.data() + start;
        }
        for (const auto& event : midi)
            if (event.sample >= start && event.sample < start + count)
                magda_device_midi(device, event.bytes.data(), static_cast<int>(event.bytes.size()),
                                  event.sample - start);
        magda_device_process(device, pointers.data(), channels, count);
    }
    magda_device_destroy(device);

    std::ofstream file(outDir + "/" + name + ".f32", std::ios::binary);
    for (const auto& channel : output)
        file.write(reinterpret_cast<const char*>(channel.data()),
                   static_cast<std::streamsize>(channel.size() * sizeof(float)));
    return static_cast<bool>(file);
}

}  // namespace

int main(int argc, char** argv) {
    std::string corpusPath, outDir;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string flag = argv[i];
        if (flag == "--corpus")
            corpusPath = argv[i + 1];
        else if (flag == "--out")
            outDir = argv[i + 1];
    }
    if (corpusPath.empty() || outDir.empty()) {
        std::cerr << "usage: render --corpus <corpus.json> --out <dir>\n";
        return 2;
    }

    std::ifstream file(corpusPath);
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string error;
    const auto corpus = magda::sdk::detail::parseJson(buffer.str(), error);
    if (!corpus) {
        std::cerr << corpusPath << ": " << error << "\n";
        return 2;
    }

    bool ok = true;
    if (const auto* cases = corpus->member("cases"))
        for (const auto& spec : cases->array)
            ok = renderCase(spec, outDir) && ok;
    return ok ? 0 : 1;
}
