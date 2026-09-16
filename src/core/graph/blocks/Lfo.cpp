#include "core/graph/Block.h"

#include <cmath>
#include <numbers>

namespace openpedal::graph::blocks {

namespace {

// lfo: low-frequency oscillator source. out = offset + depth * wave(phase), wave in [-1, 1].
class Lfo final : public Block {
public:
    static const BlockSpec& staticSpec()
    {
        static const BlockSpec s{
            .type = "lfo",
            .doc = "Low-frequency oscillator. Output is offset + depth * wave, where wave swings -1..1. "
                   "Feed it into a `delay.mod`, a `multiply`, or a filter cutoff via a knob-less node param binding.",
            .inputs = {},
            .params = {
                {.name = "shape", .defaultValue = 0, .min = 0, .max = 3,
                 .enumLabels = {"sine", "triangle", "square", "saw"}, .doc = "Waveform."},
                {.name = "rate_hz", .defaultValue = 1.0, .min = 0.01, .max = 50.0, .doc = "Frequency in Hz."},
                {.name = "depth", .defaultValue = 1.0, .min = 0.0, .max = 1000.0, .doc = "Amplitude multiplier."},
                {.name = "offset", .defaultValue = 0.0, .min = -1000.0, .max = 1000.0, .doc = "Added to the output."},
                {.name = "phase", .defaultValue = 0.0, .min = 0.0, .max = 1.0, .doc = "Starting phase in cycles."},
            }};
        return s;
    }
    const BlockSpec& spec() const override { return staticSpec(); }
    void prepare(double sr) override { sampleRate_ = sr; update(); reset(); }
    void reset() override { phase_ = startPhase_; }
    void setParam(int i, double v) override
    {
        switch (i) {
            case 0: shape_ = static_cast<int>(v); break;
            case 1: rate_ = v; update(); break;
            case 2: depth_ = v; break;
            case 3: offset_ = v; break;
            case 4: startPhase_ = v; break;
            default: break;
        }
    }
    float tick(const float*) override
    {
        double w;
        switch (shape_) {
            case 1: w = 1.0 - 4.0 * std::fabs(phase_ - 0.5); break;      // triangle
            case 2: w = phase_ < 0.5 ? 1.0 : -1.0; break;               // square
            case 3: w = 2.0 * phase_ - 1.0; break;                      // saw
            default: w = std::sin(2.0 * std::numbers::pi * phase_); break;
        }
        phase_ += inc_;
        if (phase_ >= 1.0) phase_ -= 1.0;
        return static_cast<float>(offset_ + depth_ * w);
    }

private:
    void update() { inc_ = rate_ / sampleRate_; }
    int shape_ = 0;
    double sampleRate_ = 48000.0, rate_ = 1.0, depth_ = 1.0, offset_ = 0.0, startPhase_ = 0.0;
    double phase_ = 0.0, inc_ = 0.0;
};

const BlockRegistrar<Lfo> registrar{Lfo::staticSpec()};

} // namespace

void linkLfo() {}

} // namespace openpedal::graph::blocks
