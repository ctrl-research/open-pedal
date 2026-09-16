#include "core/graph/Block.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace openpedal::graph::blocks {

namespace {

enum class Shape { Tanh, Soft, Hard, Asymmetric, Fold, Diode };

double shape(Shape s, double x)
{
    switch (s) {
        case Shape::Tanh:
            return std::tanh(x);
        case Shape::Soft: {
            // Classic cubic soft clipper, unity slope at the origin, saturates at +-1.
            if (x <= -1.5) return -1.0;
            if (x >= 1.5) return 1.0;
            return x - (4.0 / 27.0) * x * x * x;
        }
        case Shape::Hard:
            return std::clamp(x, -1.0, 1.0);
        case Shape::Asymmetric:
            // Positive half clips like a diode pair, negative half has more headroom (tube-ish).
            return x >= 0.0 ? std::tanh(x) : 2.0 * std::tanh(0.5 * x);
        case Shape::Fold: {
            // Triangle wavefolder: reflects the signal back when it exceeds +-1.
            const double t = std::fmod(std::fabs(x) + 1.0, 4.0);
            const double folded = t <= 2.0 ? t - 1.0 : 3.0 - t;
            return x < 0.0 ? -folded : folded;
        }
        case Shape::Diode:
            // Exponential knee, sharper than tanh, like a silicon diode conducting.
            return (x >= 0.0 ? 1.0 : -1.0) * (1.0 - std::exp(-std::fabs(x) * 1.5));
    }
    return x;
}

// waveshaper: static nonlinearity. Pedal-level oversampling keeps it alias-free.
class Waveshaper final : public Block {
public:
    static const BlockSpec& staticSpec()
    {
        static const BlockSpec s{
            .type = "waveshaper",
            .doc = "Static nonlinearity: out = f(drive * in + bias) - f(bias). Set the pedal's "
                   "`oversampling` to 2 or more when using this block.",
            .inputs = {"in"},
            .params = {
                {.name = "shape", .defaultValue = 0, .min = 0, .max = 5,
                 .enumLabels = {"tanh", "soft", "hard", "asymmetric", "fold", "diode"},
                 .doc = "Transfer curve."},
                {.name = "drive", .defaultValue = 1.0, .min = 0.0, .max = 1000.0, .doc = "Input gain before the curve."},
                {.name = "bias", .defaultValue = 0.0, .min = -2.0, .max = 2.0, .doc = "DC offset into the curve; makes even harmonics."},
            }};
        return s;
    }
    const BlockSpec& spec() const override { return staticSpec(); }
    void prepare(double) override {}
    void reset() override {}
    void setParam(int i, double v) override
    {
        switch (i) {
            case 0: shape_ = static_cast<Shape>(static_cast<int>(v)); break;
            case 1: drive_ = v; break;
            case 2: bias_ = v; break;
            default: return;
        }
        biasOut_ = shape(shape_, bias_);
    }
    float tick(const float* in) override
    {
        return static_cast<float>(shape(shape_, drive_ * static_cast<double>(in[0]) + bias_) - biasOut_);
    }

private:
    Shape shape_ = Shape::Tanh;
    double drive_ = 1.0, bias_ = 0.0, biasOut_ = 0.0;
};

const BlockRegistrar<Waveshaper> registrar{Waveshaper::staticSpec()};

} // namespace

void linkWaveshaper() {}

} // namespace openpedal::graph::blocks
