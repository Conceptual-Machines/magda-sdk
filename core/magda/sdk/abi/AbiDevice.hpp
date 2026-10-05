#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "magda/sdk/abi/magda_device.h"

namespace magda::sdk::host {

/// The module this program links, or null when it cannot serve this ABI version.
inline const magda_module* linkedModule() {
    return magda_module_entry(MAGDA_ABI_VERSION);
}

/**
 * @brief One device over the C ABI, with the conveniences hosts share: MIDI queued ahead of
 * process, calls of any length split into prepared blocks, text read into std::string.
 *
 * Threads follow docs/abi.md; the queue and process are audio, the rest control.
 */
class AbiDevice {
  public:
    static constexpr int kMidiCapacity = 1024;
    static constexpr std::size_t kSysexCapacity = 64 * 1024;
    static constexpr int kMaxChannels = 64;

    AbiDevice(const magda_module& module, const char* deviceType, const magda_host* host = nullptr)
        : api_(*module.api) {
        if (api_.create(deviceType, host, &device_) != MAGDA_OK)
            device_ = nullptr;
        queued_.reserve(kMidiCapacity);
        chunk_.resize(kMidiCapacity);
        queuedBytes_.resize(kSysexCapacity);
        outEvents_.resize(kMidiCapacity);
        chunkOut_.resize(kMidiCapacity);
        outBytes_.resize(kSysexCapacity);
        chunkBytes_.resize(kSysexCapacity);
    }
    ~AbiDevice() {
        if (device_ != nullptr)
            api_.destroy(device_);
    }
    AbiDevice(const AbiDevice&) = delete;
    AbiDevice& operator=(const AbiDevice&) = delete;

    explicit operator bool() const {
        return device_ != nullptr;
    }
    magda_device* get() const {
        return device_;
    }
    const magda_device_api& api() const {
        return api_;
    }

    magda_status prepare(double sampleRate, int maxFrames) {
        magda_prepare p{};
        p.struct_tag = MAGDA_TAG_PREPARE;
        p.struct_size = sizeof(p);
        p.sample_rate = sampleRate;
        p.max_frames = maxFrames;
        p.max_midi_events = kMidiCapacity;
        const auto status = api_.prepare(device_, &p);
        if (status == MAGDA_OK)
            maxFrames_ = maxFrames;
        return status;
    }

    magda_properties properties() const {
        magda_properties p{};
        p.struct_tag = MAGDA_TAG_PROPERTIES;
        p.struct_size = sizeof(p);
        api_.get_properties(device_, &p);
        return p;
    }

    /// Audio. One message at @p sample of the next process call; false when the queue is full.
    bool queueMidi(const std::uint8_t* bytes, int size, int sample) {
        if (bytes == nullptr || size <= 0 || queued_.size() >= kMidiCapacity)
            return false;
        magda_midi_event event{};
        event.sample = std::max(0, sample);
        event.size = static_cast<std::uint32_t>(size);
        if (size <= 3) {
            std::memcpy(event.short_data, bytes, static_cast<std::size_t>(size));
        } else {
            if (static_cast<std::size_t>(size) > kSysexCapacity - queuedBytesUsed_)
                return false;
            std::memcpy(queuedBytes_.data() + queuedBytesUsed_, bytes,
                        static_cast<std::size_t>(size));
            event.data = queuedBytes_.data() + queuedBytesUsed_;
            queuedBytesUsed_ += static_cast<std::size_t>(size);
        }
        queued_.push_back(event);
        return true;
    }

    /**
     * @brief Audio. Planar, in place, any frame count: split into prepared blocks, with queued
     * MIDI delivered in the block it addresses and the device's MIDI out rebased onto the call.
     */
    magda_status process(float* const* channels, int numChannels, int numFrames,
                         const magda_transport* transport = nullptr) {
        if (numChannels > kMaxChannels || numFrames < 0)
            return MAGDA_ERR_ARGUMENT;
        if (maxFrames_ <= 0)
            return MAGDA_ERR_STATE;

        // Stable insertion sort: allocation-free, linear on input already in order.
        for (auto& event : queued_)
            event.sample = std::clamp(event.sample, 0, std::max(0, numFrames - 1));
        for (std::size_t i = 1; i < queued_.size(); ++i)
            for (auto j = i; j > 0 && queued_[j - 1].sample > queued_[j].sample; --j)
                std::swap(queued_[j - 1], queued_[j]);

        outCount_ = 0;
        outBytesUsed_ = 0;
        std::size_t next = 0;
        auto status = static_cast<magda_status>(MAGDA_OK);
        for (int start = 0; start == 0 || start < numFrames; start += maxFrames_) {
            const int frames = std::min(maxFrames_, numFrames - start);
            int count = 0;
            while (next < queued_.size() && queued_[next].sample < start + frames) {
                auto event = queued_[next++];
                event.sample -= start;
                chunk_[static_cast<std::size_t>(count++)] = event;
            }

            std::array<float*, kMaxChannels> pointers{};
            for (int c = 0; c < numChannels; ++c)
                pointers[static_cast<std::size_t>(c)] = channels[c] + start;

            magda_midi_in in{chunk_.data(), count, 0};
            magda_midi_out out{};
            out.events = chunkOut_.data();
            out.capacity = kMidiCapacity - outCount_;
            out.bytes = chunkBytes_.data();
            out.byte_capacity = static_cast<std::uint32_t>(kSysexCapacity - outBytesUsed_);

            magda_process p{};
            p.struct_tag = MAGDA_TAG_PROCESS;
            p.struct_size = sizeof(p);
            p.num_frames = frames;
            p.num_channels = numChannels;
            p.channels = pointers.data();
            p.midi_in = &in;
            p.midi_out = &out;
            p.transport = transport;
            status = api_.process(device_, &p);
            if (status != MAGDA_OK)
                break;
            for (int i = 0; i < out.count; ++i)
                keepOutput(chunkOut_[static_cast<std::size_t>(i)], start);
            if (numFrames == 0)
                break;
        }
        queued_.clear();
        queuedBytesUsed_ = 0;
        return status;
    }

    /// Audio, after process: what the device emitted in that call.
    int midiOutCount() const {
        return outCount_;
    }
    const magda_midi_event& midiOut(int index) const {
        return outEvents_[static_cast<std::size_t>(index)];
    }
    static const std::uint8_t* bytesOf(const magda_midi_event& event) {
        return event.size <= 3 ? event.short_data : event.data;
    }

    int parameterCount() const {
        return api_.param_count(device_);
    }

    std::string manifest() const {
        return text(api_.get_manifest);
    }
    /// One parameter as its manifest entry; empty for a slot out of range.
    std::string parameterDescriptor(int slot) const {
        return text([this, slot](magda_device* device, char* buffer, std::int32_t capacity,
                                 std::int32_t* size) {
            return api_.param_descriptor(device, slot, buffer, capacity, size);
        });
    }
    /// The patches merged since the last take; empty when none is pending.
    std::string takeStatePatch() const {
        return text(api_.take_state_patch);
    }
    std::string state() const {
        return text(api_.get_state);
    }
    magda_status setState(std::string_view json) {
        return api_.set_state(device_, json.data(), static_cast<std::int32_t>(json.size()));
    }
    std::string lastError() const {
        return api_.last_error(device_);
    }

  private:
    void keepOutput(const magda_midi_event& event, int chunkStart) {
        if (outCount_ >= kMidiCapacity)
            return;
        auto kept = event;
        kept.sample += chunkStart;
        if (event.size > 3) {
            if (event.size > kSysexCapacity - outBytesUsed_)
                return;
            std::memcpy(outBytes_.data() + outBytesUsed_, event.data, event.size);
            kept.data = outBytes_.data() + outBytesUsed_;
            outBytesUsed_ += event.size;
        }
        outEvents_[static_cast<std::size_t>(outCount_++)] = kept;
    }

    template <typename TextFn> std::string text(TextFn fn) const {
        std::int32_t size = 0;
        if (fn(device_, nullptr, 0, &size) != MAGDA_ERR_BUFFER)
            return {};
        std::string result(static_cast<std::size_t>(size) + 1, '\0');
        if (fn(device_, result.data(), size + 1, &size) != MAGDA_OK)
            return {};
        result.resize(static_cast<std::size_t>(size));
        return result;
    }

    const magda_device_api& api_;
    magda_device* device_ = nullptr;
    int maxFrames_ = 0;

    std::vector<magda_midi_event> queued_, chunk_, outEvents_, chunkOut_;
    std::vector<std::uint8_t> queuedBytes_, outBytes_, chunkBytes_;
    std::size_t queuedBytesUsed_ = 0, outBytesUsed_ = 0;
    int outCount_ = 0;
};

}  // namespace magda::sdk::host
