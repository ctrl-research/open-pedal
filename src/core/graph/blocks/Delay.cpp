#include "core/graph/Block.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace openpedal::graph::blocks {

namespace {

// delay: interpolated delay line. `mod` input adds milliseconds to the delay time per sample,
// which is how chorus/flanger/vibrato are built (lfo -> delay.mod). Feedback is created by
// connecting a downstream node back into `in`; the graph delays that edge by one sample.
class Delay final : public Block {
public:
    static const BlockSpec& staticSpec()
    {
        static const BlockSpec s{
            .type = "delay",
            .doc = "Delay line with smooth, interpolated time changes. Connect an `lfo` to `mod` for "
                   "modulation effects; connect a later node back into `in` for feedback.",
            .inputs = {"in", "mod"},
            .params = {
                {.name = "time_ms", .defaultValue = 250.0, .min = 0.0, .max = 10000.0, .doc = "Base delay time in ms."},
                {.name = "max_ms", .defaultValue = 2000.0, .min = 1.0, .max = 10000.0, .doc = "Buffer length; time + mod is clamped to this."},
                {.name = "interpolation", .defaultValue = 1, .min = 0, .max = 1,
                 .enumLabels = {"linear", "cubic"}, .doc = "Cubic is smoother for modulated delays."},
            }};
        return s;
    }
    const BlockSpec& spec() const override { return staticSpec(); }

    void prepare(double sr) override
    {
        sampleRate_ = sr;
        allocate();
        reset();
    }
    void reset() override
    {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        write_ = 0;
    }
    void setParam(int i, double v) override
    {
        switch (i) {
            case 0: timeMs_ = v; break;
            case 1:
                if (v != maxMs_) { maxMs_ = v; allocate(); }
                break;
            case 2: cubic_ = v >= 0.5; break;
            default: break;
        }
    }
    float tick(const float* in) override
    {
        if (buffer_.empty())
            return in[0];

        const int size = static_cast<int>(buffer_.size());
        buffer_[static_cast<std::size_t>(write_)] = in[0];

        const double delayMs = std::clamp(timeMs_ + static_cast<double>(in[1]), 0.0, maxMs_);
        double delaySamples = delayMs * sampleRate_ / 1000.0;
        delaySamples = std::clamp(delaySamples, 0.0, static_cast<double>(size - 4));

        const int whole = static_cast<int>(delaySamples);
        const float frac = static_cast<float>(delaySamples - whole);
        const auto at = [&](int back) {
            int idx = write_ - back;
            while (idx < 0) idx += size;
            return buffer_[static_cast<std::size_t>(idx)];
        };

        float out;
        if (cubic_) {
            const float ym1 = at(whole - 1 < 0 ? whole : whole - 1);
            const float y0 = at(whole);
            const float y1 = at(whole + 1);
            const float y2 = at(whole + 2);
            const float c0 = y0;
            const float c1 = 0.5f * (y1 - ym1);
            const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
            const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
            out = ((c3 * frac + c2) * frac + c1) * frac + c0;
        } else {
            out = at(whole) + frac * (at(whole + 1) - at(whole));
        }

        write_ = (write_ + 1) % size;
        return out;
    }

private:
    void allocate()
    {
        const auto samples = static_cast<std::size_t>(std::ceil(maxMs_ * sampleRate_ / 1000.0)) + 8;
        buffer_.assign(samples, 0.0f);
        write_ = 0;
    }

    std::vector<float> buffer_;
    int write_ = 0;
    double sampleRate_ = 48000.0, timeMs_ = 250.0, maxMs_ = 2000.0;
    bool cubic_ = true;
};

const BlockRegistrar<Delay> registrar{Delay::staticSpec()};

} // namespace

void linkDelay() {}

} // namespace openpedal::graph::blocks
