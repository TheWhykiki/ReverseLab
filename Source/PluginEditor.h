#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

namespace rl::ui
{
// Component properties consumed by the LookAndFeel. Styling keys off these instead of matching
// control names/captions, so renaming or localising a label cannot silently change the look.
inline constexpr auto sizeControlProperty = "rl.sizeControl"; // bool: large timing control ring
inline constexpr auto violetProperty = "rl.violet";           // bool: right-channel / retrigger accent
} // namespace rl::ui

class ReverseLabLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ReverseLabLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    void drawComboBox(juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
};

class ScopeComponent final : public juce::Component, private juce::Timer
{
public:
    explicit ScopeComponent(ReverseLabAudioProcessor&);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    ReverseLabAudioProcessor& processor;
};

class ReverseLabAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ReverseLabAudioProcessorEditor(ReverseLabAudioProcessor&);
    ~ReverseLabAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void configureKnob(Knob&, const juce::String&, const char* parameterId);
    void configureButton(juce::ToggleButton&, const juce::String&, const char* parameterId,
                         std::unique_ptr<ButtonAttachment>&);
    void configureSizeControl(juce::Slider&, juce::Label&, const juce::String&, const char* parameterId,
                              std::unique_ptr<SliderAttachment>&);
    void configureFreeControl(juce::Slider&, const juce::String&, const char* parameterId,
                              std::unique_ptr<SliderAttachment>&);
    void timerCallback() override;

    ReverseLabAudioProcessor& pluginProcessor;
    ReverseLabLookAndFeel lookAndFeel;
    ScopeComponent scope;
    juce::Label title, subtitle, latencyLabel, leftSizeLabel, rightSizeLabel, presetLabel;
    juce::Slider leftSize, rightSize, leftFreeTime, rightFreeTime;
    juce::ComboBox presetBox;
    juce::ToggleButton sync, link, freeze, retrigger, bypass;
    std::array<Knob, 10> knobs;
    std::unique_ptr<SliderAttachment> leftSizeAttachment, rightSizeAttachment;
    std::unique_ptr<SliderAttachment> leftFreeAttachment, rightFreeAttachment;
    std::unique_ptr<ButtonAttachment> syncAttachment, linkAttachment, freezeAttachment,
                                      retriggerAttachment, bypassAttachment;
    bool showingSyncValues = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverseLabAudioProcessorEditor)
};
