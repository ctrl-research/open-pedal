#include "core/graph/Block.h"

#include <cmath>

namespace openpedal::graph::blocks {

namespace {

// gain: multiplies the signal by `gain` and by `gain_db` converted to linear.
class Gain final : public Block {
public:
    static const BlockSpec& staticSpec()
    {
        static const BlockSpec s{
            .type = "gain",
            .doc = "Scales the signal. Effective gain is gain * 10^(gain_db/20), so use either "
                   "the linear `gain` or the `gain_db` parameter (or both).",
            .inputs = {"in"},
            .params = {
                {.name = "gain", .defaultValue = 1.0, .min = 0.0, .max = 1000.0, .doc = "Linear gain multiplier."},
                {.name = "gain_db", .defaultValue = 0.0, .min = -120.0, .max = 60.0, .doc = "Gain in decibels."},
            }};
        return s;
    }
    const BlockSpec& spec() const override { return staticSpec(); }
    void prepare(double) override {}
    void reset() override {}
    void setParam(int i, double v) override
    {
        if (i == 0) linear_ = v;
        else if (i == 1) db_ = v;
        effective_ = static_cast<float>(linear_ * std::pow(10.0, db_ / 20.0));
    }
    float tick(const float* in) override { return in[0] * effective_; }

private:
    double linear_ = 1.0, db_ = 0.0;
    float effective_ = 1.0f;
};

const BlockRegistrar<Gain> registrar{Gain::staticSpec()};

} // namespace

void linkGain() {}

} // namespace openpedal::graph::blocks
