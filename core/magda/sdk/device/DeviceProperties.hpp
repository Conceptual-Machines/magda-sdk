#pragma once

#include <string>

#include "magda/sdk/device/SidechainPort.hpp"

namespace magda::sdk {

/// What a device is. Constant between prepares; the host re-reads it at each prepare.
struct DeviceProperties {
    std::string pluginId;
    std::string name;
    std::string shortName;
    bool takesMidiInput = false;

    /// The device emits MIDI of its own, written to ProcessContext::midiOut. Its input never
    /// passes through it: thru is the host's merge.
    bool producesMidi = false;

    /// The device copies part of its input onto its output: what it consumes is its material and
    /// the rest belongs to whatever plays its notes. Not thru; a host sizes this device's MIDI
    /// output for its input as well as its own production.
    bool forwardsMidiInput = false;

    bool takesAudioInput = true;
    bool isSynth = false;
    bool producesAudioWithoutInput = false;

    /// The sidechain slot the device asks for, if any.
    SidechainPort sidechain;

    /// Output channels the device always produces, whatever it is handed. Zero follows the input.
    int outputChannelCount = 0;

    /// Input channels the device reads for its own signal, the sidechain key not among them.
    /// Zero means the host decides.
    int inputChannelCount = 0;
};

}  // namespace magda::sdk
