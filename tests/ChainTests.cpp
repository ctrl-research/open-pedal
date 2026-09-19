#include "core/Chain.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using namespace openpedal;
using Catch::Matchers::WithinAbs;

namespace {

// Multiplies by a gain; declares an optional tail during which it emits a constant so tests
// can observe ring-out behaviour.
class GainPedal : public IPedal {
public:
    explicit GainPedal(float gain, double tailSeconds = 0.0) : gain_(gain)
    {
        desc_.id = "test.gain";
        desc_.name = "Gain";
        desc_.tailSeconds = tailSeconds;
        desc_.params.push_back(ParamDescriptor{.id = "gain", .min = 0, .max = 4, .defaultValue = 1});
    }
    const PedalDescriptor& descriptor() const override { return desc_; }
    void prepare(double, int) override { prepared_ = true; }
    void reset() override { resets_++; }
    void setParam(std::string_view id, double v) override
    {
        if (id == "gain") gain_ = static_cast<float>(v);
    }
    double getParam(std::string_view) const override { return gain_; }
    void process(float* buf, int n) override
    {
        for (int i = 0; i < n; ++i)
            buf[i] = buf[i] * gain_ + tailValue_;
    }
    bool prepared_ = false;
    int resets_ = 0;
    float tailValue_ = 0.0f;

private:
    PedalDescriptor desc_;
    float gain_;
};

std::vector<float> ones(int n) { return std::vector<float>(static_cast<std::size_t>(n), 1.0f); }

} // namespace

TEST_CASE("chain applies input gain then pedals in order")
{
    Chain chain;
    chain.setInputGainDb(-6.0206); // ~0.5
    chain.addSlot(std::make_unique<GainPedal>(2.0f), "a", true);
    chain.addSlot(std::make_unique<GainPedal>(3.0f), "b", true);
    chain.prepare(48000, 64);

    auto buf = ones(64);
    chain.process(buf.data(), 64);
    CHECK_THAT(static_cast<double>(buf[0]), WithinAbs(3.0, 1e-4));
    CHECK_THAT(static_cast<double>(buf[63]), WithinAbs(3.0, 1e-4));
}

TEST_CASE("disabled pedals and null placeholders are skipped")
{
    Chain chain;
    chain.addSlot(std::make_unique<GainPedal>(2.0f), "a", false);
    chain.addSlot(nullptr, "missing", true);
    chain.addSlot(std::make_unique<GainPedal>(3.0f), "b", true);
    chain.prepare(48000, 16);

    auto buf = ones(16);
    chain.process(buf.data(), 16);
    CHECK_THAT(static_cast<double>(buf[5]), WithinAbs(3.0, 1e-6));

    chain.setEnabled(0, true);
    buf = ones(16);
    chain.process(buf.data(), 16);
    CHECK_THAT(static_cast<double>(buf[5]), WithinAbs(6.0, 1e-6));
}

TEST_CASE("setParam reaches the pedal and ignores bad slot indices")
{
    Chain chain;
    chain.addSlot(std::make_unique<GainPedal>(1.0f), "a", true);
    chain.prepare(48000, 8);
    chain.setParam(0, "gain", 4.0);
    chain.setParam(7, "gain", 100.0);
    chain.setEnabled(-1, false);

    auto buf = ones(8);
    chain.process(buf.data(), 8);
    CHECK_THAT(static_cast<double>(buf[0]), WithinAbs(4.0, 1e-6));
}

TEST_CASE("pedals with a tail ring out on silence after being disabled, then reset")
{
    auto pedal = std::make_unique<GainPedal>(2.0f, /*tailSeconds*/ 0.001); // 48 samples at 48k
    auto* raw = pedal.get();
    raw->tailValue_ = 0.25f;

    Chain chain;
    chain.addSlot(std::move(pedal), "a", true);
    chain.prepare(48000, 32);

    chain.setEnabled(0, false);
    auto buf = ones(32);
    chain.process(buf.data(), 32);
    // Dry (1.0) + pedal's output on silence (0*2 + 0.25).
    CHECK_THAT(static_cast<double>(buf[0]), WithinAbs(1.25, 1e-6));
    CHECK(raw->resets_ == 0);

    buf = ones(32);
    chain.process(buf.data(), 32); // tail expires inside this block
    CHECK(raw->resets_ == 1);

    buf = ones(32);
    chain.process(buf.data(), 32);
    CHECK_THAT(static_cast<double>(buf[0]), WithinAbs(1.0, 1e-6)); // fully bypassed now
}

TEST_CASE("latency sums only enabled pedals")
{
    struct Latent : GainPedal {
        Latent() : GainPedal(1.0f) {}
        int latencySamples() const override { return 10; }
    };
    Chain chain;
    chain.addSlot(std::make_unique<Latent>(), "a", true);
    chain.addSlot(std::make_unique<Latent>(), "b", false);
    CHECK(chain.latencySamples() == 10);
}

TEST_CASE("group switches gate member pedals and trigger tails")
{
    auto tailPedal = std::make_unique<GainPedal>(2.0f, 0.001); // 48 samples of tail at 48k
    auto* raw = tailPedal.get();
    raw->tailValue_ = 0.25f;

    Chain chain;
    chain.addSlot(std::make_unique<GainPedal>(2.0f), "a", true, 0);  // group 0
    chain.addSlot(std::make_unique<GainPedal>(3.0f), "b", true);     // no group
    chain.addSlot(std::move(tailPedal), "c", true, 1);               // group 1
    chain.addSlot(std::make_unique<GainPedal>(5.0f), "d", false, 0); // own switch off
    chain.prepare(48000, 32);

    CHECK(chain.isActive(0));
    CHECK(chain.isActive(2));
    CHECK_FALSE(chain.isActive(3));
    auto buf = ones(32);
    chain.process(buf.data(), 32);
    CHECK_THAT(static_cast<double>(buf[0]), WithinAbs(2.0 * 3.0 * 2.0 + 0.25, 1e-6)); // c adds its constant

    chain.setGroupEnabled(0, false);
    CHECK_FALSE(chain.isActive(0));
    CHECK_FALSE(chain.groupEnabled(0));
    buf = ones(32);
    chain.process(buf.data(), 32);
    CHECK_THAT(static_cast<double>(buf[0]), WithinAbs(3.0 * 2.0 + 0.25, 1e-6));

    // Turning group 0 back on does not enable 'd', whose own switch is off.
    chain.setGroupEnabled(0, true);
    CHECK(chain.isActive(0));
    CHECK_FALSE(chain.isActive(3));

    // Group 1 off: 'c' rings out on silence, then resets.
    chain.setGroupEnabled(1, false);
    buf = ones(32);
    chain.process(buf.data(), 32);
    CHECK_THAT(static_cast<double>(buf[0]), WithinAbs(2.0 * 3.0 + 0.25, 1e-6));
    buf = ones(32);
    chain.process(buf.data(), 32);
    CHECK(raw->resets_ == 1);

    // Out-of-range groups are ignored.
    chain.setGroupEnabled(9, false);
    CHECK_FALSE(chain.groupEnabled(9));
    chain.addSlot(std::make_unique<GainPedal>(1.0f), "e", true, 99);
    CHECK(chain.slot(4).group == -1);
}
