// Build-time exporter: for every device in the module, <type>.manifest.json and the WAM 2
// <type>.descriptor.json, into --out.

#include <fstream>
#include <iostream>
#include <string>

#include "magda/sdk/abi/DeviceModule.hpp"
#include "magda/sdk/device/ParameterManifest.hpp"
#include "magda/sdk/state/detail/Json.hpp"
#include "magda/sdk/version.hpp"

namespace {

using magda::sdk::DeviceProperties;
using magda::sdk::detail::appendJsonString;

std::string descriptorJson(const DeviceProperties& properties, std::string_view deviceType,
                           const std::string& vendor, const std::string& prefix,
                           const std::string& version) {
    const bool midiIn = properties.takesMidiInput;
    const bool midiOut = properties.producesMidi || properties.forwardsMidiInput;
    std::string out = "{\n";
    const auto field = [&out](std::string_view key, std::string_view value) {
        out += "  ";
        appendJsonString(out, key);
        out += ": ";
        appendJsonString(out, value);
        out += ",\n";
    };
    const auto flag = [&out](std::string_view key, bool value, bool last = false) {
        out += "  ";
        appendJsonString(out, key);
        out += value ? ": true" : ": false";
        out += last ? "\n" : ",\n";
    };
    field("identifier", prefix + "." + std::string(deviceType));
    field("name", properties.name.empty() ? std::string(deviceType) : properties.name);
    field("vendor", vendor);
    field("version", version);
    field("apiVersion", "2.0.0");
    field("description", "");
    field("thumbnail", "");
    field("website", "");
    out += "  \"keywords\": [],\n";
    flag("isInstrument", properties.isSynth);
    flag("hasAudioInput", properties.takesAudioInput);
    flag("hasAudioOutput", true);
    flag("hasAutomationInput", true);
    flag("hasAutomationOutput", false);
    flag("hasMidiInput", midiIn);
    flag("hasMidiOutput", midiOut);
    flag("hasMpeInput", false);
    flag("hasMpeOutput", false);
    flag("hasOscInput", false);
    flag("hasOscOutput", false);
    flag("hasSysexInput", midiIn);
    flag("hasSysexOutput", midiOut, true);
    out += "}\n";
    return out;
}

bool writeFile(const std::string& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
    return static_cast<bool>(file);
}

}  // namespace

int main(int argc, char** argv) {
    std::string outDir;
    std::string vendor = "MAGDA";
    std::string prefix = "com.magda";
    std::string version(magda::sdk::versionString());
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string flag = argv[i];
        if (flag == "--out")
            outDir = argv[i + 1];
        else if (flag == "--vendor")
            vendor = argv[i + 1];
        else if (flag == "--prefix")
            prefix = argv[i + 1];
        else if (flag == "--version")
            version = argv[i + 1];
    }
    if (outDir.empty()) {
        std::cerr << "usage: manifests --out <dir> [--vendor V] [--prefix P] [--version X]\n";
        return 2;
    }

    for (const auto& factory : magda::sdk::abi::moduleDevices()) {
        const auto device = factory.create();
        const auto type = std::string(factory.deviceType);
        std::string error;
        const auto manifest = magda::sdk::writeManifest(magda::sdk::buildManifest(*device), error);
        if (!manifest) {
            std::cerr << type << ": " << error << "\n";
            return 1;
        }
        const auto descriptor =
            descriptorJson(device->properties(), factory.deviceType, vendor, prefix, version);
        if (!writeFile(outDir + "/" + type + ".manifest.json", *manifest) ||
            !writeFile(outDir + "/" + type + ".descriptor.json", descriptor)) {
            std::cerr << type << ": cannot write into " << outDir << "\n";
            return 1;
        }
    }
    return 0;
}
