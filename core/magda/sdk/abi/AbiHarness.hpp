#pragma once

#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "magda/sdk/abi/AbiDevice.hpp"

namespace magda::sdk::host {

/**
 * @brief Runs every device type of @p module through the rules of docs/abi.md that hold for any
 * device. Returns one line per broken rule; empty means the module conforms.
 */
inline std::vector<std::string> checkModuleConformance(const magda_module* module) {
    std::vector<std::string> failures;
    const auto expect = [&failures](bool ok, const std::string& what) {
        if (!ok)
            failures.push_back(what);
        return ok;
    };

    if (!expect(module != nullptr, "magda_module_entry(MAGDA_ABI_VERSION) returned null"))
        return failures;
    expect(magda_module_entry(MAGDA_ABI_VERSION + 1) == nullptr,
           "the entry serves an ABI version it does not implement");
    expect(module->struct_tag == MAGDA_TAG_MODULE && module->struct_size >= sizeof(magda_module),
           "module tag or size");
    expect(module->abi_version == MAGDA_ABI_VERSION, "module abi_version");
    expect(module->sdk_version != nullptr && module->sdk_version[0] != '\0', "sdk_version");
    if (!expect(module->api != nullptr && module->api->struct_tag == MAGDA_TAG_DEVICE_API &&
                    module->api->struct_size >= sizeof(magda_device_api),
                "api tag or size"))
        return failures;
    expect(module->get_extension != nullptr && module->get_extension("magda.analyze") == nullptr,
           "get_extension answers in version 1");
    const auto& api = *module->api;

    magda_device* none = reinterpret_cast<magda_device*>(1);
    expect(api.create("magda.no-such-type", nullptr, &none) == MAGDA_ERR_ARGUMENT &&
               none == nullptr,
           "create of an unknown type");

    std::set<std::string> ids;
    for (int t = 0; t < module->device_type_count; ++t) {
        const auto& type = module->device_types[t];
        const std::string id = type.id != nullptr ? type.id : "";
        const auto fail = [&](const std::string& what) { failures.push_back(id + ": " + what); };
        if (id.empty() || !ids.insert(id).second) {
            fail("type id empty or repeated");
            continue;
        }
        if (type.manifest == nullptr || std::strlen(type.manifest) != type.manifest_size)
            fail("type manifest missing or its size wrong");

        magda_host badHost{};
        badHost.struct_tag = MAGDA_TAG_PREPARE;
        badHost.struct_size = sizeof(badHost);
        magda_device* rejected = nullptr;
        if (api.create(type.id, &badHost, &rejected) != MAGDA_ERR_ARGUMENT || rejected != nullptr)
            fail("create accepts a host struct with the wrong tag");

        struct Notes {
            bool insideSetState = false;
            int calls = 0;
            int duringSetState = 0;
        } notes;
        magda_host host{};
        host.struct_tag = MAGDA_TAG_HOST;
        host.struct_size = sizeof(host);
        host.context = &notes;
        host.notify = [](void* context, magda_device*) {
            auto& n = *static_cast<Notes*>(context);
            ++n.calls;
            n.duringSetState += n.insideSetState ? 1 : 0;
        };

        AbiDevice device(*module, type.id, &host);
        if (!device) {
            fail("create failed");
            continue;
        }
        auto* handle = device.get();

        const auto bare = device.state();
        if (bare.find("\"device\": \"" + id + "\"") == std::string::npos ||
            bare.find("\"props\"") != std::string::npos ||
            bare.find("\"children\"") != std::string::npos)
            fail("get_state before set_state is not the bare document");

        std::int32_t size = -1;
        if (api.get_state(handle, nullptr, 0, &size) != MAGDA_ERR_BUFFER ||
            size != static_cast<std::int32_t>(bare.size()))
            fail("get_state sizing call");
        std::vector<char> exact(bare.size(), 'x');
        if (api.get_state(handle, exact.data(), static_cast<std::int32_t>(exact.size()), &size) !=
            MAGDA_ERR_BUFFER)
            fail("get_state writes without room for the terminator");
        if (!std::string(api.last_error(handle)).size())
            fail("a failing control call leaves last_error empty");

        const auto properties = device.properties();
        if (properties.struct_tag != MAGDA_TAG_PROPERTIES)
            fail("get_properties overwrote the tag");
        magda_properties shortProperties{};
        shortProperties.struct_tag = MAGDA_TAG_PROPERTIES;
        shortProperties.struct_size = 8;
        if (api.get_properties(handle, &shortProperties) != MAGDA_ERR_ARGUMENT)
            fail("get_properties accepts a struct smaller than version 1");
        if (!std::string(api.last_error(handle)).size())
            fail("last_error empty after get_properties failed");
        if (api.reset(handle) != MAGDA_OK || std::string(api.last_error(handle)).size())
            fail("a succeeding control call leaves last_error set");

        if ((properties.flags & MAGDA_PROPERTY_STATE_PARAMETERS) == 0 &&
            device.manifest() != std::string(type.manifest))
            fail("instance manifest differs from the type manifest of a static device");

        const int channels = std::max({2, properties.input_channels, properties.output_channels});
        std::vector<std::vector<float>> audio(static_cast<std::size_t>(channels),
                                              std::vector<float>(256));
        std::vector<float*> pointers;
        for (auto& channel : audio)
            pointers.push_back(channel.data());
        magda_process p{};
        p.struct_tag = MAGDA_TAG_PROCESS;
        p.struct_size = sizeof(p);
        p.num_frames = 64;
        p.num_channels = channels;
        p.channels = pointers.data();
        if (api.process(handle, &p) != MAGDA_ERR_STATE)
            fail("process before prepare is not MAGDA_ERR_STATE");

        magda_prepare badPrepare{};
        badPrepare.struct_tag = MAGDA_TAG_PROCESS;
        badPrepare.struct_size = sizeof(badPrepare);
        badPrepare.sample_rate = 48000.0;
        badPrepare.max_frames = 64;
        if (api.prepare(handle, &badPrepare) != MAGDA_ERR_ARGUMENT)
            fail("prepare accepts the wrong tag");
        if (device.prepare(48000.0, 64) != MAGDA_OK) {
            fail("prepare failed: " + device.lastError());
            continue;
        }
        api.take_notifications(handle);

        p.num_frames = 65;
        if (api.process(handle, &p) != MAGDA_ERR_ARGUMENT)
            fail("process accepts more frames than prepared");
        magda_midi_in lonelyIn{};
        p.num_frames = 64;
        p.midi_in = &lonelyIn;
        if (api.process(handle, &p) != MAGDA_ERR_ARGUMENT)
            fail("process accepts MIDI in without MIDI out");
        p.midi_in = nullptr;

        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < 256; ++i)
                audio[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] =
                    0.5f * std::sin(0.05f * static_cast<float>(i));
        if (device.process(pointers.data(), channels, 256) != MAGDA_OK)
            fail("process of 256 frames in 64-frame blocks");
        bool finite = true;
        for (const auto& channel : audio)
            for (const float sample : channel)
                finite = finite && std::isfinite(sample);
        if (!finite)
            fail("process wrote a non-finite sample");

        const int count = api.param_count(handle);
        for (int slot = 0; slot < count; ++slot) {
            std::int32_t descriptorSize = 0;
            if (api.param_descriptor(handle, slot, nullptr, 0, &descriptorSize) !=
                    MAGDA_ERR_BUFFER ||
                descriptorSize <= 0)
                fail("param_descriptor of slot " + std::to_string(slot));
            const float value = api.param_value(handle, slot);
            if (!(value >= 0.0f && value <= 1.0f))
                fail("param_value outside [0, 1] at slot " + std::to_string(slot));
            const float real = api.param_to_real(handle, slot, value);
            const float back =
                api.param_to_real(handle, slot, api.param_to_normalized(handle, slot, real));
            if (std::abs(back - real) > 1e-3f * std::max(1.0f, std::abs(real)))
                fail("reference conversion does not round-trip at slot " + std::to_string(slot));
        }
        if (api.set_param(handle, count, 0.5f) != MAGDA_ERR_ARGUMENT)
            fail("set_param accepts a slot past the count");

        const auto before = device.state();
        notes.insideSetState = true;
        const auto ownState = device.setState(before);
        const auto refused = device.setState("not json");
        notes.insideSetState = false;
        if (ownState != MAGDA_OK)
            fail("set_state refuses its own get_state: " + device.lastError());
        if (refused != MAGDA_ERR_REJECTED || device.lastError().empty())
            fail("set_state of non-JSON");
        if (device.state() != before)
            fail("a refused set_state changed the state");
        if (notes.duringSetState != 0)
            fail("notify called inside set_state");

        api.take_notifications(handle);
        std::int32_t patchSize = 0;
        if (api.take_state_patch(handle, nullptr, 0, &patchSize) != MAGDA_ERR_STATE)
            fail("take_state_patch with none pending");
        if (api.tail(handle) < -1)
            fail("tail below -1");
    }
    return failures;
}

}  // namespace magda::sdk::host
