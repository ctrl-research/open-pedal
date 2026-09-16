#include "core/graph/Block.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace openpedal::graph::blocks {

namespace {

// Robert Bristow-Johnson biquad cookbook, transposed direct form II.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    float tick(float x)
    {
        const double in = static_cast<double>(x);
        const double out = b0 * in + z1;
        z1 = b1 * in - a1 * out + z2;
        z2 = b2 * in - a2 * out;
        return static_cast<float>(out);
    }

    enum class Mode { Lowpass, Highpass, Bandpass, Notch, Peak, Lowshelf, Highshelf };

    void set(Mode mode, double sampleRate, double freq, double q, double gainDb)
    {
        freq = std::clamp(freq, 1.0, sampleRate * 0.49);
        q = std::max(q, 0.01);
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * std::numbers::pi * freq / sampleRate;
        const double cw = std::cos(w0), sw = std::sin(w0);
        const double alpha = sw / (2.0 * q);
        double a0 = 1.0;

        switch (mode) {
            case Mode::Lowpass:
                b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
                break;
            case Mode::Highpass:
                b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
                break;
            case Mode::Bandpass: // constant 0 dB peak gain
                b0 = alpha; b1 = 0; b2 = -alpha;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
                break;
            case Mode::Notch:
                b0 = 1; b1 = -2 * cw; b2 = 1;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
                break;
            case Mode::Peak:
                b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
                a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
                break;
            case Mode::Lowshelf: {
                const double beta = 2.0 * std::sqrt(A) * alpha;
                b0 = A * ((A + 1) - (A - 1) * cw + beta);
                b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - beta);
                a0 = (A + 1) + (A - 1) * cw + beta;
                a1 = -2 * ((A - 1) + (A + 1) * cw);
                a2 = (A + 1) + (A - 1) * cw - beta;
                break;
            }
            case Mode::Highshelf: {
                const double beta = 2.0 * std::sqrt(A) * alpha;
                b0 = A * ((A + 1) + (A - 1) * cw + beta);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - beta);
                a0 = (A + 1) - (A - 1) * cw + beta;
                a1 = 2 * ((A - 1) - (A + 1) * cw);
                a2 = (A + 1) - (A - 1) * cw - beta;
                break;
            }
        }
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
    }
};

class Filter final : public Block {
public:
    static const BlockSpec& staticSpec()
    {
        static const BlockSpec s{
            .type = "filter",
            .doc = "Second-order (biquad) filter. `gain_db` only applies to peak and shelf modes.",
            .inputs = {"in"},
            .params = {
                {.name = "mode", .defaultValue = 0, .min = 0, .max = 6,
                 .enumLabels = {"lowpass", "highpass", "bandpass", "notch", "peak", "lowshelf", "highshelf"},
                 .doc = "Filter response."},
                {.name = "cutoff", .defaultValue = 1000.0, .min = 1.0, .max = 40000.0, .doc = "Cutoff or centre frequency in Hz."},
                {.name = "q", .defaultValue = 0.7071, .min = 0.05, .max = 30.0, .doc = "Resonance / bandwidth."},
                {.name = "gain_db", .defaultValue = 0.0, .min = -40.0, .max = 40.0, .doc = "Boost or cut for peak/shelf modes."},
            }};
        return s;
    }
    const BlockSpec& spec() const override { return staticSpec(); }
    void prepare(double sr) override { sampleRate_ = sr; update(); }
    void reset() override { bq_.reset(); }
    void setParam(int i, double v) override
    {
        switch (i) {
            case 0: mode_ = static_cast<Biquad::Mode>(static_cast<int>(v)); break;
            case 1: cutoff_ = v; break;
            case 2: q_ = v; break;
            case 3: gainDb_ = v; break;
            default: return;
        }
        update();
    }
    float tick(const float* in) override { return bq_.tick(in[0]); }

private:
    void update() { bq_.set(mode_, sampleRate_, cutoff_, q_, gainDb_); }

    Biquad bq_;
    Biquad::Mode mode_ = Biquad::Mode::Lowpass;
    double sampleRate_ = 48000.0, cutoff_ = 1000.0, q_ = 0.7071, gainDb_ = 0.0;
};

// tilt: one-knob tone control. Positive tilt brightens (cuts lows, boosts highs).
class Tilt final : public Block {
public:
    static const BlockSpec& staticSpec()
    {
        static const BlockSpec s{
            .type = "tilt",
            .doc = "One-knob tone control: a low shelf and a high shelf pivoting around `center`. "
                   "Positive `tilt_db` brightens, negative darkens.",
            .inputs = {"in"},
            .params = {
                {.name = "center", .defaultValue = 1000.0, .min = 20.0, .max = 20000.0, .doc = "Pivot frequency in Hz."},
                {.name = "tilt_db", .defaultValue = 0.0, .min = -24.0, .max = 24.0, .doc = "High-shelf gain; the low shelf gets the opposite."},
            }};
        return s;
    }
    const BlockSpec& spec() const override { return staticSpec(); }
    void prepare(double sr) override { sampleRate_ = sr; update(); }
    void reset() override { lo_.reset(); hi_.reset(); }
    void setParam(int i, double v) override
    {
        if (i == 0) center_ = v;
        else if (i == 1) tilt_ = v;
        update();
    }
    float tick(const float* in) override { return hi_.tick(lo_.tick(in[0])); }

private:
    void update()
    {
        lo_.set(Biquad::Mode::Lowshelf, sampleRate_, center_, 0.7071, -tilt_ * 0.5);
        hi_.set(Biquad::Mode::Highshelf, sampleRate_, center_, 0.7071, tilt_ * 0.5);
    }
    Biquad lo_, hi_;
    double sampleRate_ = 48000.0, center_ = 1000.0, tilt_ = 0.0;
};

// dc_block: one-pole high-pass, removes the offset that asymmetric clipping introduces.
class DcBlock final : public Block {
public:
    static const BlockSpec& staticSpec()
    {
        static const BlockSpec s{
            .type = "dc_block",
            .doc = "One-pole high-pass filter that removes DC offset, e.g. after asymmetric clipping.",
            .inputs = {"in"},
            .params = {
                {.name = "cutoff", .defaultValue = 10.0, .min = 0.1, .max = 200.0, .doc = "Corner frequency in Hz."},
            }};
        return s;
    }
    const BlockSpec& spec() const override { return staticSpec(); }
    void prepare(double sr) override { sampleRate_ = sr; update(); }
    void reset() override { x1_ = y1_ = 0.0; }
    void setParam(int i, double v) override
    {
        if (i == 0) { cutoff_ = v; update(); }
    }
    float tick(const float* in) override
    {
        const double x = static_cast<double>(in[0]);
        const double y = x - x1_ + r_ * y1_;
        x1_ = x;
        y1_ = y;
        return static_cast<float>(y);
    }

private:
    void update() { r_ = 1.0 - (2.0 * std::numbers::pi * cutoff_ / sampleRate_); }
    double sampleRate_ = 48000.0, cutoff_ = 10.0, r_ = 0.999;
    double x1_ = 0.0, y1_ = 0.0;
};

const BlockRegistrar<Filter> filterRegistrar{Filter::staticSpec()};
const BlockRegistrar<Tilt> tiltRegistrar{Tilt::staticSpec()};
const BlockRegistrar<DcBlock> dcRegistrar{DcBlock::staticSpec()};

} // namespace

void linkFilter() {}

} // namespace openpedal::graph::blocks
