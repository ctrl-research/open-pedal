#include "SlotParameter.h"

#include <cmath>

namespace openpedal {

namespace {

juce::String slotParamId(int slot, int knob)
{
    return "slot" + juce::String(slot + 1) + "_knob" + juce::String(knob + 1);
}

juce::String formatReal(const ParamDescriptor& d, double real)
{
    switch (d.type) {
        case ParamType::Bool:
            return real >= 0.5 ? "On" : "Off";
        case ParamType::Enum:
            if (auto label = d.enumLabel(real))
                return juce::String(std::string(*label));
            return juce::String(static_cast<int>(real));
        case ParamType::Int:
            return juce::String(static_cast<int>(std::lround(real))) + (d.unit.empty() ? "" : " " + juce::String(d.unit));
        case ParamType::Float:
            break;
    }
    const double span = std::fabs(d.max - d.min);
    const int decimals = span >= 1000 ? 0 : span >= 100 ? 1 : 2;
    return juce::String(real, decimals) + (d.unit.empty() ? "" : " " + juce::String(d.unit));
}

} // namespace

SlotParameter::SlotParameter(int slot, int knob)
    : juce::RangedAudioParameter(juce::ParameterID{slotParamId(slot, knob), 1},
                                 "Slot " + juce::String(slot + 1) + " Knob " + juce::String(knob + 1)),
      slot_(slot),
      knob_(knob)
{
}

void SlotParameter::setDescriptor(const ParamDescriptor* d, const juce::String& pedalName)
{
    pedalName_ = pedalName;
    descriptor_.store(d, std::memory_order_release);
}

double SlotParameter::realValue() const
{
    const auto* d = descriptor();
    return d ? d->toReal(static_cast<double>(getValue())) : 0.0;
}

void SlotParameter::setRealValueNotifyingHost(double real)
{
    if (const auto* d = descriptor())
        setValueNotifyingHost(static_cast<float>(d->toNormalised(real)));
}

void SlotParameter::setRealValue(double real)
{
    if (const auto* d = descriptor())
        setValue(static_cast<float>(d->toNormalised(real)));
}

bool SlotParameter::consumeChange()
{
    return changed_.exchange(false, std::memory_order_acq_rel);
}

void SlotParameter::setValue(float newValue)
{
    value_.store(juce::jlimit(0.0f, 1.0f, newValue), std::memory_order_relaxed);
    changed_.store(true, std::memory_order_release);
}

float SlotParameter::getDefaultValue() const
{
    const auto* d = descriptor();
    return d ? static_cast<float>(d->toNormalised(d->defaultValue)) : 0.0f;
}

juce::String SlotParameter::getName(int maximumStringLength) const
{
    const auto* d = descriptor();
    juce::String name = juce::String(slot_ + 1) + ": ";
    if (d)
        name += (pedalName_.isNotEmpty() ? pedalName_ + " " : juce::String()) + juce::String(d->name);
    else
        name += "(empty knob " + juce::String(knob_ + 1) + ")";
    return name.substring(0, maximumStringLength);
}

juce::String SlotParameter::getLabel() const
{
    const auto* d = descriptor();
    return d ? juce::String(d->unit) : juce::String();
}

int SlotParameter::getNumSteps() const
{
    const auto* d = descriptor();
    if (!d || !d->isDiscrete())
        return juce::AudioProcessor::getDefaultNumParameterSteps();
    return static_cast<int>(std::lround(d->max - d->min)) + 1;
}

bool SlotParameter::isDiscrete() const
{
    const auto* d = descriptor();
    return d && d->isDiscrete();
}

bool SlotParameter::isBoolean() const
{
    const auto* d = descriptor();
    return d && d->type == ParamType::Bool;
}

bool SlotParameter::isAutomatable() const
{
    const auto* d = descriptor();
    return !d || d->automatable;
}

juce::String SlotParameter::getText(float normalisedValue, int maximumStringLength) const
{
    const auto* d = descriptor();
    if (!d)
        return juce::String(normalisedValue, 2).substring(0, maximumStringLength);
    return formatReal(*d, d->toReal(static_cast<double>(normalisedValue))).substring(0, maximumStringLength);
}

float SlotParameter::getValueForText(const juce::String& text) const
{
    const auto* d = descriptor();
    if (!d)
        return juce::jlimit(0.0f, 1.0f, text.getFloatValue());
    if (d->type == ParamType::Enum) {
        if (auto idx = d->enumIndex(text.trim().toStdString()))
            return static_cast<float>(d->toNormalised(*idx));
    }
    if (d->type == ParamType::Bool)
        return text.trim().equalsIgnoreCase("on") || text.getIntValue() != 0 ? 1.0f : 0.0f;
    return static_cast<float>(d->toNormalised(text.retainCharacters("0123456789.-+eE").getDoubleValue()));
}

SlotBypassParameter::SlotBypassParameter(int slot)
    : juce::AudioParameterBool(juce::ParameterID{"slot" + juce::String(slot + 1) + "_bypass", 1},
                               "Slot " + juce::String(slot + 1) + " Bypass", false),
      slot_(slot)
{
}

bool SlotBypassParameter::consumeChange()
{
    return changed_.exchange(false, std::memory_order_acq_rel);
}

juce::String SlotBypassParameter::getName(int maximumStringLength) const
{
    juce::String name = juce::String(slot_ + 1) + ": " + (pedalName_.isNotEmpty() ? pedalName_ + " " : juce::String("(empty) ")) + "Bypass";
    return name.substring(0, maximumStringLength);
}

GroupParameter::GroupParameter(int group)
    : juce::AudioParameterBool(juce::ParameterID{"group" + juce::String(group + 1) + "_on", 1},
                               "Group " + juce::String(group + 1) + " On", true),
      group_(group)
{
}

bool GroupParameter::consumeChange()
{
    return changed_.exchange(false, std::memory_order_acq_rel);
}

juce::String GroupParameter::getName(int maximumStringLength) const
{
    const juce::String label = groupName_.isNotEmpty() ? groupName_ : "Group " + juce::String(group_ + 1);
    return ("Group: " + label + " On").substring(0, maximumStringLength);
}

} // namespace openpedal
