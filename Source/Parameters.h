#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace rl::params
{
inline constexpr auto sync = "sync";
inline constexpr auto link = "link";
inline constexpr auto leftSize = "leftSize";
inline constexpr auto rightSize = "rightSize";
inline constexpr auto leftFreeMs = "leftFreeMs";
inline constexpr auto rightFreeMs = "rightFreeMs";
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

// Every parameter carries a version hint so the identifiers stay stable if an AU build is ever
// added (the VST3 wrapper derives its IDs from the string alone and is unaffected).
inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using APF = juce::AudioParameterFloat;
    using APB = juce::AudioParameterBool;
    using APC = juce::AudioParameterChoice;
    using API = juce::AudioParameterInt;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<APB>(juce::ParameterID { sync, 1 }, "Tempo Sync", true));
    layout.add(std::make_unique<APB>(juce::ParameterID { link, 1 }, "Link Left/Right", true));
    layout.add(std::make_unique<APC>(juce::ParameterID { leftSize, 1 }, "Left Size", subdivisionNames(), 8));
    layout.add(std::make_unique<APC>(juce::ParameterID { rightSize, 1 }, "Right Size", subdivisionNames(), 8));
    layout.add(std::make_unique<APF>(juce::ParameterID { leftFreeMs, 1 }, "Left Free Time", juce::NormalisableRange<float>(20.0f, 4000.0f, 0.1f, 0.35f), 500.0f, "ms"));
    layout.add(std::make_unique<APF>(juce::ParameterID { rightFreeMs, 1 }, "Right Free Time", juce::NormalisableRange<float>(20.0f, 4000.0f, 0.1f, 0.35f), 500.0f, "ms"));
    layout.add(std::make_unique<APF>(juce::ParameterID { speed, 1 }, "Reverse Speed", juce::NormalisableRange<float>(0.25f, 4.0f, 0.001f, 0.45f), 1.0f, "x"));
    layout.add(std::make_unique<APF>(juce::ParameterID { crossfade, 1 }, "Crossfade", juce::NormalisableRange<float>(0.0f, 25.0f, 0.01f), 4.0f, "%"));
    layout.add(std::make_unique<APF>(juce::ParameterID { mix, 1 }, "Dry/Wet", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 100.0f, "%"));
    layout.add(std::make_unique<APF>(juce::ParameterID { output, 1 }, "Output", juce::NormalisableRange<float>(-24.0f, 12.0f, 0.01f), 0.0f, "dB"));
    layout.add(std::make_unique<APB>(juce::ParameterID { freeze, 1 }, "Freeze", false));
    layout.add(std::make_unique<APB>(juce::ParameterID { retrigger, 1 }, "Retrigger", false));
    layout.add(std::make_unique<APF>(juce::ParameterID { feedback, 1 }, "Feedback", juce::NormalisableRange<float>(0.0f, 95.0f, 0.01f), 0.0f, "%"));
    layout.add(std::make_unique<APF>(juce::ParameterID { highpass, 1 }, "High-pass", juce::NormalisableRange<float>(20.0f, 1000.0f, 0.01f, 0.35f), 20.0f, "Hz"));
    layout.add(std::make_unique<APF>(juce::ParameterID { lowpass, 1 }, "Low-pass", juce::NormalisableRange<float>(1000.0f, 20000.0f, 0.01f, 0.35f), 20000.0f, "Hz"));
    layout.add(std::make_unique<APF>(juce::ParameterID { stereoOffset, 1 }, "Stereo Offset", juce::NormalisableRange<float>(-100.0f, 100.0f, 0.01f), 0.0f, "%"));
    layout.add(std::make_unique<APF>(juce::ParameterID { random, 1 }, "Random", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 0.0f, "%"));
    layout.add(std::make_unique<API>(juce::ParameterID { seed, 1 }, "Random Seed", 1, 999999, 4242));
    layout.add(std::make_unique<APB>(juce::ParameterID { bypass, 1 }, "Bypass", false));
    return layout;
}
} // namespace rl::params
