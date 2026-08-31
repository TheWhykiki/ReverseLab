#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace rl::params
{
inline constexpr auto sync = "sync";
inline constexpr auto link = "link";
inline constexpr auto leftSize = "leftSize";
inline constexpr auto rightSize = "rightSize";
inline constexpr auto speed = "speed";
inline constexpr auto crossfade = "crossfade";
inline constexpr auto mix = "mix";
inline constexpr auto output = "output";
inline constexpr auto freeze = "freeze";
inline constexpr auto retrigger = "retrigger";
inline constexpr auto feedback = "feedback";
inline constexpr auto highpass = "highpass";
inline constexpr auto lowpass = "lowpass";
inline constexpr auto stereoOffset = "stereoOffset";
inline constexpr auto random = "random";
inline constexpr auto seed = "seed";
inline constexpr auto bypass = "bypass";

inline juce::StringArray subdivisionNames()
{
    return { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
             "1/4T", "1/4", "1/4D", "1/2T", "1/2", "1/2D", "1 Bar", "2 Bars" };
}

inline constexpr std::array<double, 15> subdivisionBeats {
    0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75,
    2.0 / 3.0, 1.0, 1.5, 4.0 / 3.0, 2.0, 3.0, 4.0, 8.0
};

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using APF = juce::AudioParameterFloat;
    using APB = juce::AudioParameterBool;
    using APC = juce::AudioParameterChoice;
    using API = juce::AudioParameterInt;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<APB>(sync, "Tempo Sync", true));
    layout.add(std::make_unique<APB>(link, "Link Left/Right", true));
    layout.add(std::make_unique<APC>(leftSize, "Left Size", subdivisionNames(), 8));
    layout.add(std::make_unique<APC>(rightSize, "Right Size", subdivisionNames(), 8));
    layout.add(std::make_unique<APF>(speed, "Reverse Speed", juce::NormalisableRange<float>(0.25f, 4.0f, 0.001f, 0.45f), 1.0f, "x"));
    layout.add(std::make_unique<APF>(crossfade, "Crossfade", juce::NormalisableRange<float>(0.0f, 25.0f, 0.01f), 4.0f, "%"));
    layout.add(std::make_unique<APF>(mix, "Dry/Wet", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 100.0f, "%"));
    layout.add(std::make_unique<APF>(output, "Output", juce::NormalisableRange<float>(-24.0f, 12.0f, 0.01f), 0.0f, "dB"));
    layout.add(std::make_unique<APB>(freeze, "Freeze", false));
    layout.add(std::make_unique<APB>(retrigger, "Retrigger", false));
    layout.add(std::make_unique<APF>(feedback, "Feedback", juce::NormalisableRange<float>(0.0f, 95.0f, 0.01f), 0.0f, "%"));
    layout.add(std::make_unique<APF>(highpass, "High-pass", juce::NormalisableRange<float>(20.0f, 1000.0f, 0.01f, 0.35f), 20.0f, "Hz"));
    layout.add(std::make_unique<APF>(lowpass, "Low-pass", juce::NormalisableRange<float>(1000.0f, 20000.0f, 0.01f, 0.35f), 20000.0f, "Hz"));
    layout.add(std::make_unique<APF>(stereoOffset, "Stereo Offset", juce::NormalisableRange<float>(-100.0f, 100.0f, 0.01f), 0.0f, "%"));
    layout.add(std::make_unique<APF>(random, "Random", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 0.0f, "%"));
    layout.add(std::make_unique<API>(seed, "Random Seed", 1, 999999, 4242));
    layout.add(std::make_unique<APB>(bypass, "Bypass", false));
    return layout;
}
} // namespace rl::params
