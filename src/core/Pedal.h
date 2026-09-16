#pragma once

#include "core/PedalDescriptor.h"

#include <string_view>

namespace openpedal {

// The one interface every pedal backend implements (JSON graph today, Faust or native later).
//
// Audio is mono, in place, 32-bit float. Parameters arrive in real units as declared by the
// descriptor; the pedal is responsible for its own smoothing. Everything except process()
// is called from a non-realtime thread. process() must not allocate, lock, or block.
class IPedal {
public:
    virtual ~IPedal() = default;

    virtual const PedalDescriptor& descriptor() const = 0;

    virtual void prepare(double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;

    // Set a knob to a real-unit value. Unknown ids are ignored.
    virtual void setParam(std::string_view paramId, double realValue) = 0;
    virtual double getParam(std::string_view paramId) const = 0;

    virtual void process(float* buffer, int numSamples) = 0;

    // Samples of latency introduced by this pedal at the current sample rate (oversampling etc.).
    virtual int latencySamples() const { return 0; }
};

} // namespace openpedal
