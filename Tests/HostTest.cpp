#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
using Setting = std::pair<const char*, float>;
constexpr Setting settings[] {
    { "Tempo Sync", 0.0f }, { "Link Left/Right", 0.0f },
    { "Left Size", 0.64f }, { "Right Size", 0.14f },
    { "Left Free Time", 0.18f }, { "Right Free Time", 0.30f },
    { "Reverse Speed", 0.42f }, { "Crossfade", 0.16f },
    { "Dry/Wet", 1.0f }, { "Output", 2.0f / 3.0f },
    { "Freeze", 0.0f }, { "Retrigger", 0.0f }, { "Feedback", 0.0f },
    { "High-pass", 0.0f }, { "Low-pass", 1.0f },
    { "Stereo Offset", 0.5f }, { "Random", 0.0f }, { "Random Seed", 0.125f }, { "Bypass", 0.0f }
};

void require(bool condition, const char* message)
{
    if (! condition) throw std::runtime_error(message);
}

juce::AudioProcessorParameter& parameter(juce::AudioProcessor& processor, const char* name)
{
    for (auto* item : processor.getParameters())
        if (item->getName(128) == name) return *item;
    throw std::runtime_error(std::string("Missing VST3 parameter: ") + name);
}

bool finiteSamples(const juce::AudioBuffer<float>& audio)
{
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            if (! std::isfinite(audio.getSample(channel, sample))) return false;
    return true;
}

std::vector<float> render(juce::AudioPluginInstance& plugin)
{
    constexpr int blockSize = 256, totalSamples = 96000;
    plugin.prepareToPlay(48000.0, blockSize);
    juce::AudioBuffer<float> audio(2, blockSize);
    juce::MidiBuffer midi;
    std::vector<float> result;
    result.reserve(totalSamples * 2);
    double energy = 0.0;
    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const auto count = juce::jmin(blockSize, totalSamples - offset);
        audio.setSize(2, count, false, false, true);
        for (int sample = 0; sample < count; ++sample)
        {
            const auto time = static_cast<double>(offset + sample) / 48000.0;
            audio.setSample(0, sample, static_cast<float>(0.2 * std::sin(time * 1382.300767579509)));
            audio.setSample(1, sample, static_cast<float>(0.15 * std::sin(time * 2073.451151369263)));
        }
        plugin.processBlock(audio, midi);
        require(finiteSamples(audio), "VST3 emitted a nonfinite audio sample");
        for (int sample = 0; sample < count; ++sample)
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto value = audio.getSample(channel, sample);
                require(std::abs(value) < 2.0f, "Unexpected VST3 output bound");
                energy += static_cast<double>(value) * value;
                result.push_back(value);
            }
    }
    require(energy / static_cast<double>(result.size()) > 1.0e-6, "VST3 render is silent");
    plugin.releaseResources();
    return result;
}
}

int main(int argc, char** argv)
{
    try
    {
        require(argc == 2, "Usage: ReverseLabHostTests <actual VST3 bundle>");
        juce::ScopedJuceInitialiser_GUI initialiseJuce;
        // Calibrate the validator: a peak reduction alone can hide NaN samples.
        juce::AudioBuffer<float> invalid(2, 8);
        invalid.clear();
        invalid.setSample(1, 5, std::numeric_limits<float>::quiet_NaN());
        require(! finiteSamples(invalid), "NaN validator failed calibration");
        std::fprintf(stderr, "[host-test] scanning %s\n", argv[1]);
        juce::VST3PluginFormat scanner;
        juce::OwnedArray<juce::PluginDescription> descriptions;
        scanner.findAllTypesForFile(descriptions, argv[1]);
        require(descriptions.size() == 1, "Expected exactly one VST3 effect");
        require(descriptions[0]->name == "ReverseLab", "Unexpected VST3 name");
        juce::AudioPluginFormatManager manager;
        manager.addFormat(std::make_unique<juce::VST3PluginFormat>());
        juce::String error;
        auto instance = manager.createPluginInstance(*descriptions[0], 48000.0, 256, error);
        require(instance != nullptr, error.isEmpty() ? "VST3 creation failed" : error.toRawUTF8());
        require(instance->getTotalNumInputChannels() == 2 && instance->getTotalNumOutputChannels() == 2,
                "VST3 stereo bus layout is incorrect");
        std::vector<float> expectedParameters;
        for (const auto& setting : settings)
        {
            auto& value = parameter(*instance, setting.first);
            // The VST3 controller can cache an unsnapped request while APVTS
            // persists its legal grid value. Ask the plugin's own formatter/parser
            // for a canonical request instead of treating that difference as loss.
            const auto text = value.getText(setting.second, 128);
            const auto canonical = value.getValueForText(text);
            require(std::isfinite(canonical) && canonical >= 0.0f && canonical <= 1.0f,
                    "VST3 parameter returned an invalid canonical value");
            if (std::abs(canonical - setting.second) > 1.0e-7f)
                std::fprintf(stderr, "[host-test] canonical %s: request=%g text='%s' value=%g\n",
                             setting.first, static_cast<double>(setting.second), text.toRawUTF8(),
                             static_cast<double>(canonical));
            value.setValueNotifyingHost(canonical);
            expectedParameters.push_back(value.getValue());
        }
        juce::MemoryBlock state;
        instance->getStateInformation(state);
        require(state.getSize() > 0, "VST3 state is empty");
        const auto before = render(*instance);
        instance.reset();
        auto restored = manager.createPluginInstance(*descriptions[0], 48000.0, 256, error);
        require(restored != nullptr, "VST3 recreation failed");
        restored->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        size_t index = 0;
        for (const auto& setting : settings)
        {
            const auto expected = expectedParameters[index++];
            const auto actual = parameter(*restored, setting.first).getValue();
            if (std::abs(actual - expected) >= 1.0e-5f)
            {
                std::fprintf(stderr, "[host-test] %s: expected=%g restored=%g\n", setting.first,
                             static_cast<double>(expected), static_cast<double>(actual));
                throw std::runtime_error("VST3 parameter state did not roundtrip");
            }
        }
        const auto after = render(*restored);
        require(before.size() == after.size(), "VST3 render length changed");
        for (size_t sample = 0; sample < before.size(); ++sample)
            require(std::abs(before[sample] - after[sample]) <= 1.0e-6f,
                    "VST3 restored-state audio differs from initial render");
        restored.reset();
        std::fprintf(stderr, "[host-test] bundle load, finite render and state/audio roundtrip passed\n");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "[host-test] %s\n", error.what());
        return 1;
    }
}
