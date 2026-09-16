#include "core/ParamDescriptor.h"

#include <algorithm>
#include <cmath>

namespace openpedal {

std::string_view toString(ParamType t)
{
    switch (t) {
        case ParamType::Float: return "float";
        case ParamType::Int: return "int";
        case ParamType::Bool: return "bool";
        case ParamType::Enum: return "enum";
    }
    return "float";
}

std::string_view toString(Taper t)
{
    switch (t) {
        case Taper::Linear: return "linear";
        case Taper::Log: return "log";
        case Taper::Audio: return "audio";
    }
    return "linear";
}

std::optional<ParamType> parseParamType(std::string_view s)
{
    if (s == "float") return ParamType::Float;
    if (s == "int") return ParamType::Int;
    if (s == "bool") return ParamType::Bool;
    if (s == "enum") return ParamType::Enum;
    return std::nullopt;
}

std::optional<Taper> parseTaper(std::string_view s)
{
    if (s == "linear") return Taper::Linear;
    if (s == "log") return Taper::Log;
    if (s == "audio") return Taper::Audio;
    return std::nullopt;
}

double ParamDescriptor::clamp(double real) const
{
    const double lo = std::min(min, max);
    const double hi = std::max(min, max);
    double v = std::clamp(real, lo, hi);
    if (isDiscrete())
        v = std::round(v);
    return v;
}

double ParamDescriptor::toNormalised(double real) const
{
    const double v = clamp(real);
    if (max == min)
        return 0.0;

    switch (taper) {
        case Taper::Linear:
            return (v - min) / (max - min);
        case Taper::Log: {
            if (min <= 0.0 || max <= 0.0)
                return (v - min) / (max - min); // invalid log range, fall back to linear
            return std::log(v / min) / std::log(max / min);
        }
        case Taper::Audio: {
            const double lin = (v - min) / (max - min);
            return std::sqrt(std::max(0.0, lin));
        }
    }
    return 0.0;
}

double ParamDescriptor::toReal(double normalised) const
{
    const double n = std::clamp(normalised, 0.0, 1.0);
    double v = min;

    switch (taper) {
        case Taper::Linear:
            v = min + (max - min) * n;
            break;
        case Taper::Log:
            if (min <= 0.0 || max <= 0.0)
                v = min + (max - min) * n;
            else
                v = min * std::pow(max / min, n);
            break;
        case Taper::Audio:
            v = min + (max - min) * n * n;
            break;
    }
    return clamp(v);
}

std::optional<int> ParamDescriptor::enumIndex(std::string_view label) const
{
    if (type != ParamType::Enum)
        return std::nullopt;
    const auto it = std::find(enumValues.begin(), enumValues.end(), label);
    if (it == enumValues.end())
        return std::nullopt;
    return static_cast<int>(std::distance(enumValues.begin(), it));
}

std::optional<std::string_view> ParamDescriptor::enumLabel(double real) const
{
    if (type != ParamType::Enum || enumValues.empty())
        return std::nullopt;
    const auto idx = static_cast<std::size_t>(clamp(real));
    if (idx >= enumValues.size())
        return std::nullopt;
    return std::string_view(enumValues[idx]);
}

bool ParamDescriptor::validate(std::string& error) const
{
    if (id.empty()) {
        error = "knob has an empty id";
        return false;
    }
    if (type == ParamType::Enum) {
        if (enumValues.size() < 2) {
            error = "enum knob '" + id + "' needs at least two values";
            return false;
        }
        if (min != 0.0 || max != static_cast<double>(enumValues.size() - 1)) {
            error = "enum knob '" + id + "' range must be 0.." + std::to_string(enumValues.size() - 1);
            return false;
        }
    } else if (type == ParamType::Bool) {
        if (min != 0.0 || max != 1.0) {
            error = "bool knob '" + id + "' range must be 0..1";
            return false;
        }
    }
    if (!(max > min)) {
        error = "knob '" + id + "' has max <= min";
        return false;
    }
    if (taper == Taper::Log && (min <= 0.0 || max <= 0.0)) {
        error = "knob '" + id + "' uses a log taper but its range includes zero or negatives";
        return false;
    }
    if (defaultValue < min || defaultValue > max) {
        error = "knob '" + id + "' default is outside its range";
        return false;
    }
    if (smoothingMs < 0.0) {
        error = "knob '" + id + "' has negative smoothing_ms";
        return false;
    }
    return true;
}

} // namespace openpedal
