#pragma once

namespace magda::sdk {

/// Read-only musical time, valid for the duration of a process call. Audio thread.
class TempoMap {
  public:
    virtual ~TempoMap() = default;

    virtual double beatsAtSeconds(double seconds) const = 0;
    virtual double bpmAtSeconds(double seconds) const = 0;
};

}  // namespace magda::sdk
