#pragma once

#include "magda/sdk/state/StateNode.hpp"

namespace magda::sdk {

/**
 * @brief What a device may ask of the host that runs it.
 *
 * Every call is made on the control thread, never from process(); a device that wants to report
 * from the audio thread queues the report and sends it from the control thread.
 */
class DeviceHost {
  public:
    virtual ~DeviceHost() = default;

    /**
     * @brief The device produced state of its own that the host's document does not hold.
     *
     * The host owns the state document and the device restores it; this is the one way state
     * flows back, for something the host could not have authored.
     */
    virtual void stateChanged(StateNode state) = 0;

    /// The device's properties, latency or channel layout changed; the host re-prepares it.
    virtual void rebuildRequired() = 0;
};

}  // namespace magda::sdk
