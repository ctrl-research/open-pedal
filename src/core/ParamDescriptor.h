#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openpedal {

enum class ParamType : std::uint8_t { Float, Int, Bool, Enum };

// How a knob's travel (normalised 0..1) maps onto its real-unit range.
//   Linear: even travel.
//   Log:    equal travel per octave; requires min > 0. Use for Hz and ms.
//   Audio:  quadratic, more resolution near the bottom. Use for linear gains and mix amounts.
enum class Taper : std::uint8_t { Linear, Log, Audio };

std::string_view toString(ParamType t);
std::string_view toString(Taper t);
std::optional<ParamType> parseParamType(std::string_view s);
std::optional<Taper> parseTaper(std::string_view s);

// Describes one user-facing knob, switch, or selector on a pedal.
// Values exchanged through setParam() and stored in boards are always in real units
// (Hz, ms, dB, or unit-less), never normalised. Enums are represented as their index.
struct ParamDescriptor {
    std::string id;
    std::string name;
    ParamType type = ParamType::Float;
    double min = 0.0;
    double max = 1.0;
    double defaultValue = 0.0;
    std::string unit;
    Taper taper = Taper::Linear;
    double smoothingMs = 20.0;
    bool automatable = true;
    std::vector<std::string> enumValues; // only for ParamType::Enum

    // Clamp (and for Int/Bool/Enum, round) a real value into the legal range.
    double clamp(double real) const;

    // Real <-> normalised 0..1 through the taper. Both clamp their input.
    double toNormalised(double real) const;
    double toReal(double normalised) const;

    // Enum helpers. Return nullopt if the label is unknown or the type is not Enum.
    std::optional<int> enumIndex(std::string_view label) const;
    std::optional<std::string_view> enumLabel(double real) const;

    // Whether the descriptor is internally consistent (range, taper, enum values, default).
    // Fills `error` with a human-readable reason when returning false.
    bool validate(std::string& error) const;

    bool isDiscrete() const { return type != ParamType::Float; }
};

} // namespace openpedal
