#pragma once

#include "core/Pedal.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace openpedal {

// A prepared, realtime-safe series chain of pedals.
//
// Built entirely off the audio thread (constructor + prepare), then handed to the audio thread,
// which only calls process(), setEnabled(), and setParam(). Those never allocate.
//
// Bypass: a disabled pedal is skipped, except that pedals declaring a tail keep running on
// silence for tailSeconds after being disabled so delays and reverbs ring out naturally.
class Chain {
public:
    struct Slot {
        std::unique_ptr<IPedal> pedal; // may be null: a placeholder for an unresolved pedal
        std::string pedalId;
        bool enabled = true;
        int tailSamplesRemaining = 0;
    };

    Chain() = default;

    // Add a pedal. Pass nullptr to add a bypass placeholder for a pedal that could not be loaded.
    void addSlot(std::unique_ptr<IPedal> pedal, std::string pedalId, bool enabled);

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    void setInputGainDb(double db);
    double inputGainDb() const { return inputGainDb_; }

    int numSlots() const { return static_cast<int>(slots_.size()); }
    Slot& slot(int index) { return slots_[static_cast<std::size_t>(index)]; }
    const Slot& slot(int index) const { return slots_[static_cast<std::size_t>(index)]; }

    // Realtime-safe controls.
    void setEnabled(int slotIndex, bool enabled);
    void setParam(int slotIndex, std::string_view paramId, double realValue);

    // Process a mono buffer in place.
    void process(float* buffer, int numSamples);

    // Total latency of all enabled pedals.
    int latencySamples() const;

private:
    std::vector<Slot> slots_;
    std::vector<float> scratch_; // used for tail rendering (pedal on silence)
    double sampleRate_ = 44100.0;
    double inputGainDb_ = 0.0;
    std::atomic<float> inputGainLinear_{1.0f};
};

} // namespace openpedal
