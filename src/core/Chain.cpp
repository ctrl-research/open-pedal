#include "core/Chain.h"

#include <algorithm>
#include <cmath>

namespace openpedal {

void Chain::addSlot(std::unique_ptr<IPedal> pedal, std::string pedalId, bool enabled)
{
    Slot s;
    s.pedal = std::move(pedal);
    s.pedalId = std::move(pedalId);
    s.enabled = enabled;
    slots_.push_back(std::move(s));
}

void Chain::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    scratch_.assign(static_cast<std::size_t>(std::max(1, maxBlockSize)), 0.0f);
    for (auto& s : slots_) {
        if (s.pedal)
            s.pedal->prepare(sampleRate, maxBlockSize);
        s.tailSamplesRemaining = 0;
    }
}

void Chain::reset()
{
    for (auto& s : slots_) {
        if (s.pedal)
            s.pedal->reset();
        s.tailSamplesRemaining = 0;
    }
}

void Chain::setInputGainDb(double db)
{
    inputGainDb_ = db;
    inputGainLinear_.store(static_cast<float>(std::pow(10.0, db / 20.0)), std::memory_order_relaxed);
}

void Chain::setEnabled(int slotIndex, bool enabled)
{
    if (slotIndex < 0 || slotIndex >= numSlots())
        return;
    auto& s = slots_[static_cast<std::size_t>(slotIndex)];
    if (s.enabled == enabled)
        return;
    s.enabled = enabled;
    if (!enabled && s.pedal) {
        const double tail = s.pedal->descriptor().tailSeconds;
        s.tailSamplesRemaining = tail > 0.0 ? static_cast<int>(std::ceil(tail * sampleRate_)) : 0;
    }
}

void Chain::setParam(int slotIndex, std::string_view paramId, double realValue)
{
    if (slotIndex < 0 || slotIndex >= numSlots())
        return;
    auto& s = slots_[static_cast<std::size_t>(slotIndex)];
    if (s.pedal)
        s.pedal->setParam(paramId, realValue);
}

void Chain::process(float* buffer, int numSamples)
{
    if (numSamples <= 0)
        return;

    const float gain = inputGainLinear_.load(std::memory_order_relaxed);
    if (gain != 1.0f)
        for (int i = 0; i < numSamples; ++i)
            buffer[i] *= gain;

    for (auto& s : slots_) {
        if (!s.pedal)
            continue;

        if (s.enabled) {
            s.pedal->process(buffer, numSamples);
            continue;
        }

        if (s.tailSamplesRemaining > 0) {
            // Ring out: feed the pedal silence and add what comes back to the dry signal.
            const int n = std::min(numSamples, static_cast<int>(scratch_.size()));
            std::fill(scratch_.begin(), scratch_.begin() + n, 0.0f);
            s.pedal->process(scratch_.data(), n);
            for (int i = 0; i < n; ++i)
                buffer[i] += scratch_[static_cast<std::size_t>(i)];
            s.tailSamplesRemaining -= n;
            if (s.tailSamplesRemaining <= 0) {
                s.tailSamplesRemaining = 0;
                s.pedal->reset();
            }
        }
    }
}

int Chain::latencySamples() const
{
    int total = 0;
    for (const auto& s : slots_)
        if (s.pedal && s.enabled)
            total += s.pedal->latencySamples();
    return total;
}

} // namespace openpedal
