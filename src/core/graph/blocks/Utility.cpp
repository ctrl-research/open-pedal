#include "core/graph/Block.h"

#include <algorithm>
#include <cmath>

namespace openpedal::graph::blocks {

namespace {

// mix: crossfade between two inputs.
class Mix final : public Block {
public:
    static const BlockSpec& staticSpec()
    {
        static const BlockSpec s{
            .type = "mix",
            .doc = "Crossfade: out = a * (1 - mix) + b * mix. Typical use: a = dry, b = wet.",
            .inputs = {"a", "b"},
            .params = {{.name = "mix", .defaultValue = 0.5, .min = 0.0, .max = 1.0, .doc = "0 = only a, 1 = only b."}}};
        return s;
    }
    const BlockSpec& spec() const override { return staticSpec(); }
    void prepare(double) override {}
    void reset() override {}
    void setParam(int i, double v) override { if (i == 0) mix_ = static_cast<float>(v); }
    float tick(const float* in) override { return in[0] * (1.0f - mix_) + in[1] * mix_; }

private:
    float mix_ = 0.5f;
};

// multiply: ring-mod / VCA. Tremolo is signal * lfo(offset 0.5, depth 0.5).
class Multiply final : public Block {
public:
    static const BlockSpec& staticSpec()
    {
        static const BlockSpec s{
            .type = "multiply",
            .doc = "out = a * b. Use with an `lfo` for tremolo or with an `envelope` for dynamics.",
            .inputs = {"a", "b"},
            .params = {}};
        return s;
    }
    const BlockSpec& spec() const override { return staticSpec(); }
    void prepare(double) override {}
    void reset() override {}
    void setParam(int, double) override {}
    float tick(const float* in) override { return in[0] * in[1]; }
};

// clamp: hard limits.
class Clamp final : public Block {
public:
    static const BlockSpec& staticSpec()
    {
        static const BlockSpec s{
            .type = "clamp",
            .doc = "Hard-limits the signal between min and max.",
            .inputs = {"in"},
            .params = {
                {.name = "min", .defaultValue = -1.0, .min = -100.0, .max = 100.0, .doc = "Lower bound."},
                {.name = "max", .defaultValue = 1.0, .min = -100.0, .max = 100.0, .doc = "Upper bound."},
            }};
        return s;
    }
    const BlockSpec& spec() const override { return staticSpec(); }
    void prepare(double) override {}
    void reset() override {}
    void setParam(int i, double v) override
    {
        if (i == 0) lo_ = static_cast<float>(v);
        else if (i == 1) hi_ = static_cast<float>(v);
    }
    float tick(const float* in) override { return std::clamp(in[0], std::min(lo_, hi_), std::max(lo_, hi_)); }

private:
    float lo_ = -1.0f, hi_ = 1.0f;
};

// envelope: peak follower with attack/release, outputs a positive control signal.
class Envelope final : public Block {
public:
    static const BlockSpec& staticSpec()
    {
        static const BlockSpec s{
            .type = "envelope",
            .doc = "Peak envelope follower. Output is the smoothed absolute level of the input, "
                   "a control signal for `multiply` or for driving other blocks.",
            .inputs = {"in"},
            .params = {
                {.name = "attack_ms", .defaultValue = 10.0, .min = 0.01, .max = 5000.0, .doc = "Rise time."},
                {.name = "release_ms", .defaultValue = 100.0, .min = 0.01, .max = 10000.0, .doc = "Fall time."},
            }};
        return s;
    }
    const BlockSpec& spec() const override { return staticSpec(); }
    void prepare(double sr) override { sampleRate_ = sr; update(); }
    void reset() override { env_ = 0.0; }
    void setParam(int i, double v) override
    {
        if (i == 0) attackMs_ = v;
        else if (i == 1) releaseMs_ = v;
        update();
    }
    float tick(const float* in) override
    {
        const double x = std::fabs(static_cast<double>(in[0]));
        const double coef = x > env_ ? attackCoef_ : releaseCoef_;
        env_ = x + coef * (env_ - x);
        return static_cast<float>(env_);
    }

private:
    void update()
    {
        attackCoef_ = std::exp(-1.0 / (attackMs_ * 0.001 * sampleRate_));
        releaseCoef_ = std::exp(-1.0 / (releaseMs_ * 0.001 * sampleRate_));
    }
    double sampleRate_ = 48000.0, attackMs_ = 10.0, releaseMs_ = 100.0;
    double attackCoef_ = 0.0, releaseCoef_ = 0.0, env_ = 0.0;
};

const BlockRegistrar<Mix> mixRegistrar{Mix::staticSpec()};
const BlockRegistrar<Multiply> multiplyRegistrar{Multiply::staticSpec()};
const BlockRegistrar<Clamp> clampRegistrar{Clamp::staticSpec()};
const BlockRegistrar<Envelope> envelopeRegistrar{Envelope::staticSpec()};

} // namespace

void linkUtility() {}

} // namespace openpedal::graph::blocks
