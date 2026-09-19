#pragma once

#include "core/ParamDescriptor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace openpedal {

// A host-visible parameter for one knob position in one chain slot.
//
// VST3 hosts require a fixed parameter list, so the plugin exposes kMaxSlots x kMaxKnobs of these
// plus one bypass per slot. Each one is retargeted at whatever knob currently occupies that
// position: `descriptor()` is swapped on the message thread when the board changes and the host
// is told parameter names changed. Values are normalised 0..1; the descriptor supplies the
// mapping to real units, the display text, and the step count for enums.
class SlotParameter final : public juce::RangedAudioParameter {
public:
    SlotParameter(int slot, int knob);

    int slot() const { return slot_; }
    int knob() const { return knob_; }

    // Message thread. Pass nullptr when the slot has no pedal or the pedal has fewer knobs.
    void setDescriptor(const ParamDescriptor* d, const juce::String& pedalName);
    const ParamDescriptor* descriptor() const { return descriptor_.load(std::memory_order_acquire); }

    // Real-unit accessors through the current descriptor. Real value is 0 with no descriptor.
    double realValue() const;
    void setRealValueNotifyingHost(double real);
    void setRealValue(double real); // no host notification (state restore)

    // Audio thread: returns true once per change so the processor can push it to the chain.
    bool consumeChange();

    // juce::AudioProcessorParameter
    float getValue() const override { return value_.load(std::memory_order_relaxed); }
    void setValue(float newValue) override;
    float getDefaultValue() const override;
    juce::String getName(int maximumStringLength) const override;
    juce::String getLabel() const override;
    int getNumSteps() const override;
    bool isDiscrete() const override;
    bool isBoolean() const override;
    bool isAutomatable() const override;
    juce::String getText(float normalisedValue, int maximumStringLength) const override;
    float getValueForText(const juce::String& text) const override;
    const juce::NormalisableRange<float>& getNormalisableRange() const override { return range_; }

private:
    const int slot_;
    const int knob_;
    juce::NormalisableRange<float> range_{0.0f, 1.0f};
    std::atomic<float> value_{0.0f};
    std::atomic<bool> changed_{false};
    std::atomic<const ParamDescriptor*> descriptor_{nullptr};
    juce::String pedalName_;
};

// Per-slot bypass switch exposed to the host.
class SlotBypassParameter final : public juce::AudioParameterBool {
public:
    explicit SlotBypassParameter(int slot);
    int slot() const { return slot_; }
    bool consumeChange();
    void setPedalName(const juce::String& name) { pedalName_ = name; }
    juce::String getName(int maximumStringLength) const override;

private:
    void valueChanged(bool) override { changed_.store(true, std::memory_order_release); }
    const int slot_;
    std::atomic<bool> changed_{false};
    juce::String pedalName_;
};

// Per-group on/off switch exposed to the host. On by default.
class GroupParameter final : public juce::AudioParameterBool {
public:
    explicit GroupParameter(int group);
    int group() const { return group_; }
    bool consumeChange();
    void setGroupName(const juce::String& name) { groupName_ = name; }
    juce::String getName(int maximumStringLength) const override;

private:
    void valueChanged(bool) override { changed_.store(true, std::memory_order_release); }
    const int group_;
    std::atomic<bool> changed_{false};
    juce::String groupName_;
};

} // namespace openpedal
