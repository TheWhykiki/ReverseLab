#include "Parameters.h"
#include <cmath>
#include <iostream>

namespace
{
class ParameterHost final : public juce::AudioProcessor
{
public:
    ParameterHost() : state(*this, nullptr, "TextTest", rl::params::createLayout()) {}
    const juce::String getName() const override { return "Parameter text test"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    juce::AudioProcessorValueTreeState state;
};
}

int main()
{
    juce::ScopedJuceInitialiser_GUI initialise;
    ParameterHost host;
    int failures = 0;
    auto check = [&failures](bool passed, const juce::String& description)
    {
        if (!passed)
        {
            std::cerr << "FAIL: " << description << '\n';
            ++failures;
        }
    };

    for (const auto* id : {rl::params::highpass, rl::params::lowpass})
    {
        auto& parameter = *host.state.getParameter(id);
        const auto offValue = parameter.convertFrom0to1(parameter.getDefaultValue());
        juce::Slider slider;
        juce::SliderParameterAttachment attachment(parameter, slider);
        for (const auto& text : {juce::String("Off"), juce::String("off"), juce::String(" OFF ")})
        {
            const auto parsed = parameter.convertFrom0to1(parameter.getValueForText(text));
            check(std::abs(parsed - offValue) < 0.01f,
                  juce::String(id) + " must parse '" + text + "' as bypass, got " + juce::String(parsed));
            check(std::abs(slider.getValueFromText(text) - offValue) < 0.01,
                  juce::String(id) + " slider/accessibility parser must accept Off");
        }
        check(parameter.getText(parameter.getDefaultValue(), 0) == "Off",
              juce::String(id) + " host and editor must share the Off label");
        const auto cutoff = juce::String(id) == rl::params::lowpass ? 6800.25f : 110.25f;
        const auto text = parameter.getText(parameter.convertTo0to1(cutoff), 0);
        check(text.endsWith(" Hz"), "Active filter text includes its unit");
        check(std::abs(parameter.convertFrom0to1(parameter.getValueForText(text)) - cutoff) < 0.011f,
              juce::String(id) + " fractional cutoff survives text roundtrip");
    }

    // Exercise the complete actual parameter layout, not a separate mock formatter.
    for (auto* raw : host.getParameters())
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(raw);
        check(parameter != nullptr, "Every parameter must be ranged");
        if (parameter == nullptr) continue;
        for (int step = 0; step <= 100; ++step)
        {
            const auto value = parameter->convertFrom0to1(static_cast<float>(step) / 100.0f);
            const auto text = parameter->getText(parameter->convertTo0to1(value), 0);
            const auto restored = parameter->convertFrom0to1(parameter->getValueForText(text));
            const auto tolerance = juce::jmax(0.00001f, parameter->getNormalisableRange().interval * 1.01f);
            check(std::isfinite(restored) && std::abs(restored - value) <= tolerance,
                  parameter->getName(80) + " text roundtrip: " + text);
        }
    }
    std::cout << "Parameter text checks: " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
