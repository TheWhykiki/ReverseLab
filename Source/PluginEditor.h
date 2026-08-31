#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

class ReverseLabLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ReverseLabLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
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
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void configureKnob(Knob&, const juce::String&, const char* parameterId);
    void configureButton(juce::ToggleButton&, const juce::String&, const char* parameterId,
                         std::unique_ptr<ButtonAttachment>&);
    void timerCallback() override;

    ReverseLabAudioProcessor& processor;
    ReverseLabLookAndFeel lookAndFeel;
    ScopeComponent scope;
    juce::Label title, subtitle, latencyLabel, leftSizeLabel, rightSizeLabel, presetLabel;
    juce::ComboBox leftSize, rightSize, presetBox;
    juce::ToggleButton sync, link, freeze, retrigger, bypass;
    std::array<Knob, 9> knobs;
    std::unique_ptr<ComboAttachment> leftSizeAttachment, rightSizeAttachment;
    std::unique_ptr<ButtonAttachment> syncAttachment, linkAttachment, freezeAttachment,
                                      retriggerAttachment, bypassAttachment;
    bool showingSyncValues = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverseLabAudioProcessorEditor)
};
