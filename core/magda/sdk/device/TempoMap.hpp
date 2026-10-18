#pragma once

namespace magda::sdk {

/// Read-only musical time, valid for the duration of a process call. Audio thread.
class TempoMap {
  public:
    virtual ~TempoMap() = default;

    virtual double beatsAtSeconds(double seconds) const = 0;
    virtual double bpmAtSeconds(double seconds) const = 0;

    /// Quarter-note beats in the bar at @p seconds; 4/4 for a host that does not say.
    virtual double beatsPerBarAtSeconds(double /*seconds*/) const {
        return 4.0;
    }
};

}  // namespace magda::sdk
