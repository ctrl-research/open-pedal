#pragma once

#include "core/Pedal.h"
#include "core/PedalDefinition.h"
#include "core/graph/GraphPedal.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

namespace test {

inline std::unique_ptr<openpedal::graph::GraphPedal> compilePedal(const std::string& jsonText)
{
    auto parsed = openpedal::parsePedalString(jsonText);
    if (!parsed.ok()) {
        for (const auto& e : parsed.errors)
            UNSCOPED_INFO(e);
        REQUIRE(parsed.ok());
    }
    std::vector<std::string> errors;
    auto pedal = openpedal::graph::GraphPedal::compile(*parsed.definition, errors);
    for (const auto& e : errors)
        UNSCOPED_INFO(e);
    REQUIRE(pedal != nullptr);
    return pedal;
}

// Parse + compile and return only the error list (empty means it compiled).
inline std::vector<std::string> compileErrors(const std::string& jsonText)
{
    auto parsed = openpedal::parsePedalString(jsonText);
    if (!parsed.ok())
        return parsed.errors;
    std::vector<std::string> errors;
    openpedal::graph::GraphPedal::compile(*parsed.definition, errors);
    return errors;
}

inline std::vector<float> sine(double freqHz, double sampleRate, int n, float amp = 0.5f)
{
    std::vector<float> v(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        v[static_cast<std::size_t>(i)] = amp * static_cast<float>(std::sin(2.0 * std::numbers::pi * freqHz * i / sampleRate));
    return v;
}

inline std::vector<float> impulse(int n)
{
    std::vector<float> v(static_cast<std::size_t>(n), 0.0f);
    v[0] = 1.0f;
    return v;
}

inline double rms(const std::vector<float>& v, std::size_t from = 0)
{
    double acc = 0.0;
    for (std::size_t i = from; i < v.size(); ++i)
        acc += static_cast<double>(v[i]) * static_cast<double>(v[i]);
    return std::sqrt(acc / static_cast<double>(v.size() - from));
}

inline bool allFinite(const std::vector<float>& v)
{
    for (float x : v)
        if (!std::isfinite(x))
            return false;
    return true;
}

inline void processInBlocks(openpedal::IPedal& pedal, std::vector<float>& buf, int block = 64)
{
    const int n = static_cast<int>(buf.size());
    for (int pos = 0; pos < n; pos += block)
        pedal.process(buf.data() + pos, std::min(block, n - pos));
}

inline bool contains(const std::vector<std::string>& errors, const std::string& needle)
{
    for (const auto& e : errors)
        if (e.find(needle) != std::string::npos)
            return true;
    return false;
}

} // namespace test
