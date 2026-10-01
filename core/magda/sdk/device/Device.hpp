#pragma once

#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "magda/sdk/device/DeviceHost.hpp"
#include "magda/sdk/device/DeviceProperties.hpp"
#include "magda/sdk/device/Parameters.hpp"
#include "magda/sdk/device/ProcessContext.hpp"
#include "magda/sdk/state/StateNode.hpp"
#include "magda/sdk/telemetry/Telemetry.hpp"

namespace magda::sdk {

/// A tail that never decays, such as a frozen reverb or a held drone.
inline constexpr std::int64_t kInfiniteTail = std::numeric_limits<std::int64_t>::max();

/// What a prepare is told.
struct PrepareContext {
    double sampleRate = 44100.0;
    int maximumBlockSize = 0;
};

/// How a restore went. A failed restore leaves the device in the state it had before.
struct RestoreResult {
    bool ok = true;
    std::string message;

    static RestoreResult success() {
        return {};
    }
    static RestoreResult error(std::string message) {
        return {false, std::move(message)};
    }
};

/**
 * @brief The device contract: JUCE-free, host-independent (docs/device-interface.md).
 *
 * Threads. Control: every call except process() and setParameter*(), made one at a time and
 * never concurrently with process(), except where noted. Audio: process() and the parameter
 * setters, in block order. A device guards whatever the two share.
 *
 * Lifecycle. setHost, then prepare, then process any number of times, then release; prepare may
 * follow release again. properties() is read before prepare, latencySamples() after it.
 *
 * Allocation. process() and the parameter setters neither allocate, lock, nor block.
 */
class Device {
  public:
    virtual ~Device() = default;

    /// Control. Before prepare; null on detach. The host outlives its attachment.
    virtual void setHost(DeviceHost* host) {
        (void)host;
    }

    /// Control. Constant between prepares; to change it, ask for DeviceHost::rebuildRequired.
    virtual DeviceProperties properties() const = 0;

    /// Control. Allocate everything process() uses here.
    virtual void prepare(const PrepareContext& context) {
        (void)context;
    }

    /// Control. Pairs with prepare; a device may be prepared again afterwards.
    virtual void release() {}

    /// Control, not concurrent with process(). Drop history: delay lines, voices, tails.
    virtual void reset() {}

    /// Control. Samples of latency, valid after prepare.
    virtual int latencySamples() const {
        return 0;
    }

    /// Control. Samples the output rings after the input stops, or kInfiniteTail. May change
    /// between prepares (a loaded impulse, a release time); the host reads it when it needs it.
    virtual std::int64_t tailSamples() const {
        return 0;
    }

    /// Audio. Allocation-free.
    virtual void process(ProcessContext& context) = 0;

    /// Control. Parameters are addressed by slot, in [0, parameterCount()), values normalized to
    /// [0, 1].
    virtual int parameterCount() const {
        return 0;
    }

    /// Control. Whether @p slot is a parameter the device offers now; a pool a device fills at
    /// runtime answers false for an empty slot.
    virtual bool offersParameter(int slot) const {
        (void)slot;
        return true;
    }

    /// Control. How @p slot is described: the source of the parameter manifest. Slots are those
    /// of parameterCount().
    virtual ParameterDescriptor parameterDescriptor(int slot) const {
        (void)slot;
        return {};
    }

    virtual float parameterValue(int slot) const {
        (void)slot;
        return 0.0f;
    }

    /// Audio, before process(): the parameter's value for the block.
    virtual void setParameterValue(int slot, float normalized) {
        (void)slot;
        (void)normalized;
    }

    /**
     * @brief Audio, before process(): the parameter's value as sample-accurate segments.
     *
     * The default applies the first segment's start value through setParameterValue.
     */
    virtual void setParameterSegments(int slot, std::span<const ParameterSegment> segments) {
        if (!segments.empty())
            setParameterValue(slot, segments.front().startValue);
    }

    /**
     * @brief Control. Adopt the host's saved state.
     *
     * All or nothing: on error the device keeps the state it had. Absent properties and children
     * read as the device's defaults. Never reads automatable parameters from it.
     */
    virtual RestoreResult restoreState(const StateNode& state) {
        (void)state;
        return RestoreResult::success();
    }

    /// Control. A telemetry surface by key, owned by the device for its lifetime, or null.
    virtual DeviceTelemetry* telemetry(std::string_view key) {
        (void)key;
        return nullptr;
    }
    virtual const DeviceTelemetry* telemetry(std::string_view key) const {
        (void)key;
        return nullptr;
    }
};

}  // namespace magda::sdk
