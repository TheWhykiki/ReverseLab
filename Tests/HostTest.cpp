#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
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

bool isNormalizedParameterValue(float value)
{
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

bool parameterStateMatches(float expected, float actual)
{
    // Reject invalid operands before subtraction: abs(NaN) >= tolerance is false.
    return isNormalizedParameterValue(expected) && isNormalizedParameterValue(actual)
           && std::abs(actual - expected) < 1.0e-5f;
}

bool parameterRequestMatches(float requested, float actual)
{
    // Controller readback must acknowledge the canonical request, not merely return
    // some valid default. This is stricter than the existing state roundtrip tolerance.
    return isNormalizedParameterValue(requested) && isNormalizedParameterValue(actual)
           && std::abs(actual - requested) < 1.0e-6f;
}

void requireParameterRequest(float requested, float actual, const char* name)
{
    if (! parameterRequestMatches(requested, actual))
    {
        std::fprintf(stderr, "[host-test] ignored/changed request %s: requested=%g readback=%g\n",
                     name, static_cast<double>(requested), static_cast<double>(actual));
        throw std::runtime_error("VST3 did not apply the canonical parameter request");
    }
}

std::vector<float> readParameters(juce::AudioProcessor& processor)
{
    std::vector<float> values;
    for (const auto& setting : settings)
    {
        const auto value = parameter(processor, setting.first).getValue();
        require(isNormalizedParameterValue(value), "VST3 parameter readback is invalid");
        values.push_back(value);
    }
    return values;
}

void requireFreshDefaultsDiffer(const std::vector<float>& saved, const std::vector<float>& fresh)
{
    require(saved.size() == std::size(settings) && fresh.size() == saved.size(),
            "Fresh-default comparison has an invalid parameter layout");
    bool differs = false;
    for (size_t i = 0; i < saved.size(); ++i)
    {
        require(isNormalizedParameterValue(saved[i]) && isNormalizedParameterValue(fresh[i]),
                "Fresh-default comparison contains an invalid parameter value");
        differs = differs || ! parameterStateMatches(saved[i], fresh[i]);
    }
    require(differs, "Fresh VST3 defaults already match the recall fixture; skipped restore would pass");
}

template <typename Action>
void requireRejected(Action action, const char* message)
{
    bool rejected = false;
    try { action(); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, message);
}

void calibrateParameterStateValidator()
{
    require(parameterStateMatches(0.0f, 0.0f) && parameterStateMatches(1.0f, 1.0f)
            && parameterStateMatches(0.5f, 0.5f + 5.0e-6f),
            "Parameter validator rejected a valid normalized control");
    require(! parameterStateMatches(0.5f, 0.5f + 2.0e-5f),
            "Parameter validator accepted a difference outside the existing tolerance");
    for (const auto invalid : { std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity(),
                                -std::numeric_limits<float>::infinity() })
    {
        require(! parameterStateMatches(0.5f, invalid),
                "Parameter validator accepted a nonfinite restored value");
        require(! parameterStateMatches(invalid, 0.5f),
                "Parameter validator accepted a nonfinite saved value");
        require(! parameterStateMatches(invalid, invalid),
                "Parameter validator accepted a nonfinite value pair");
    }
    for (const auto invalid : { -0.01f, 1.01f })
    {
        require(! parameterStateMatches(invalid, invalid),
                "Parameter validator accepted matching values outside the normalized range");
        require(! parameterStateMatches(0.5f, invalid)
                && ! parameterStateMatches(invalid, 0.5f),
                "Parameter validator accepted an operand outside the normalized range");
    }
    std::fprintf(stderr, "[host-test] parameter validator: normalized controls and NaN/+Inf/-Inf in both operands passed\n");
    require(parameterRequestMatches(0.18f, 0.18f), "Setter validator rejected an exact request");
    requireRejected([] { requireParameterRequest(0.18f, 0.5f, "no-op setter control"); },
                    "Setter validator accepted a no-op write returning a valid default");
    const std::vector<float> unchanged(std::size(settings), 0.5f);
    requireRejected([&] { requireFreshDefaultsDiffer(unchanged, unchanged); },
                    "Fresh-default validator accepted a fixture equal to defaults");
    auto different = unchanged;
    different[0] = 0.0f;
    requireFreshDefaultsDiffer(different, unchanged);
    for (const auto invalid : { std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity(), -0.01f, 1.01f })
        require(! parameterRequestMatches(invalid, invalid), "Setter validator accepted invalid values");
    std::fprintf(stderr, "[host-test] setter/default controls: ignored request and ineffective recall fixture rejected\n");
}

constexpr int renderFrames = 96000;

double fixturePhase(int channel, int sample)
{
    return static_cast<double>(sample) / 48000.0
           * (channel == 0 ? 1382.300767579509 : 2073.451151369263);
}

float fixtureSample(int channel, int sample)
{
    return static_cast<float>((channel == 0 ? 0.2 : 0.15) * std::sin(fixturePhase(channel, sample)));
}

double nonDryMeanSquare(const std::vector<float>& audio, int channel)
{
    // A latency-compensated bypass is not identical to the unshifted input. In the
    // settled second half, any fixed gain/delay of our single-frequency input is
    // still a combination of its sine/cosine basis. Non-unit reverse speed must
    // produce energy outside that dry subspace. This is only an effect-path gate;
    // no such fitting/alignment is applied to the independent state/audio comparison.
    constexpr int start = renderFrames / 2;
    double ss = 0.0, cc = 0.0, sc = 0.0, ys = 0.0, yc = 0.0;
    for (int sample = start; sample < renderFrames; ++sample)
    {
        const auto s = std::sin(fixturePhase(channel, sample));
        const auto c = std::cos(fixturePhase(channel, sample));
        const auto value = static_cast<double>(audio[static_cast<size_t>(sample * 2 + channel)]);
        ss += s * s; cc += c * c; sc += s * c; ys += value * s; yc += value * c;
    }
    const auto determinant = ss * cc - sc * sc;
    require(determinant > 0.0 && std::isfinite(determinant), "Invalid dry comparison basis");
    const auto sineGain = (ys * cc - yc * sc) / determinant;
    const auto cosineGain = (yc * ss - ys * sc) / determinant;
    double residual = 0.0;
    for (int sample = start; sample < renderFrames; ++sample)
    {
        const auto value = static_cast<double>(audio[static_cast<size_t>(sample * 2 + channel)]);
        const auto delta = value - sineGain * std::sin(fixturePhase(channel, sample))
                                 - cosineGain * std::cos(fixturePhase(channel, sample));
        residual += delta * delta;
    }
    return residual / (renderFrames - start);
}

void requireWetStereoAudio(const std::vector<float>& audio, bool report = false)
{
    require(audio.size() == static_cast<size_t>(renderFrames * 2), "Unexpected wet render geometry");
    std::array<double, 2> energy {}, dryDifference {};
    for (int sample = 0; sample < renderFrames; ++sample)
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto value = audio[static_cast<size_t>(sample * 2 + channel)];
            require(std::isfinite(value) && std::abs(value) < 2.0f, "Invalid wet audio sample");
            const auto delta = static_cast<double>(value) - fixtureSample(channel, sample);
            energy[static_cast<size_t>(channel)] += static_cast<double>(value) * value;
            dryDifference[static_cast<size_t>(channel)] += delta * delta;
        }
    for (const auto channelEnergy : energy)
        require(channelEnergy / renderFrames > 1.0e-6, "VST3 wet output channel is silent");
    for (size_t channel = 0; channel < energy.size(); ++channel)
    {
        // This fixture requests 100% wet, distinct free windows and non-unit reverse
        // speed. Its output must differ materially from untouched input on BOTH channels.
        // Recall equality alone would also accept a broken pass-through processBlock.
        require(dryDifference[channel] / renderFrames > 1.0e-6,
                "VST3 wet output channel matches dry passthrough");
        const auto nonDry = nonDryMeanSquare(audio, static_cast<int>(channel));
        require(std::isfinite(nonDry) && nonDry > 1.0e-6,
                "VST3 wet output channel is only a gain/delay of dry input");
        if (report)
            std::fprintf(stderr, "[host-test] wet channel %zu: RMS=%g dry-difference RMS=%g non-dry RMS=%g\n",
                         channel, std::sqrt(energy[channel] / renderFrames),
                         std::sqrt(dryDifference[channel] / renderFrames), std::sqrt(nonDry));
    }
}

void calibrateWetStereoValidator()
{
    std::vector<float> dry(static_cast<size_t>(renderFrames * 2)), positive;
    for (int sample = 0; sample < renderFrames; ++sample)
        for (int channel = 0; channel < 2; ++channel)
            dry[static_cast<size_t>(sample * 2 + channel)] = fixtureSample(channel, sample);
    positive = dry;
    for (int sample = 0; sample < renderFrames; ++sample)
        for (int channel = 0; channel < 2; ++channel)
            positive[static_cast<size_t>(sample * 2 + channel)] = fixtureSample(channel, sample / 2);
    requireWetStereoAudio(positive); // Validator control, not a reference reverse algorithm.
    requireRejected([&] { requireWetStereoAudio(dry); }, "Wet validator accepted stereo passthrough");
    auto delayedDry = dry;
    for (int sample = 0; sample < renderFrames; ++sample)
        for (int channel = 0; channel < 2; ++channel)
            delayedDry[static_cast<size_t>(sample * 2 + channel)] = sample < 3200 ? 0.0f
                : 0.4f * fixtureSample(channel, sample - 3200);
    requireRejected([&] { requireWetStereoAudio(delayedDry); }, "Wet validator accepted gain/delayed dry output");
    for (int channel = 0; channel < 2; ++channel)
    {
        auto dead = positive;
        for (int sample = 0; sample < renderFrames; ++sample)
            dead[static_cast<size_t>(sample * 2 + channel)] = 0.0f;
        requireRejected([&] { requireWetStereoAudio(dead); }, "Wet validator accepted a silent channel");
        auto dryChannel = positive;
        for (int sample = 0; sample < renderFrames; ++sample)
            dryChannel[static_cast<size_t>(sample * 2 + channel)] = fixtureSample(channel, sample);
        requireRejected([&] { requireWetStereoAudio(dryChannel); }, "Wet validator accepted one dry channel");
    }
    for (const auto invalid : { std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity(),
                                -std::numeric_limits<float>::infinity() })
    {
        auto bad = positive;
        bad.back() = invalid;
        requireRejected([&] { requireWetStereoAudio(bad); }, "Wet validator accepted a nonfinite sample");
    }
    std::fprintf(stderr, "[host-test] wet controls: passthrough, gain/delayed dry, either silent/dry channel and NaN/+Inf/-Inf rejected\n");
}

std::vector<float> render(juce::AudioPluginInstance& plugin)
{
    constexpr int blockSize = 256, totalSamples = renderFrames;
    const auto configured = readParameters(plugin);
    plugin.prepareToPlay(48000.0, blockSize);
    const auto prepared = readParameters(plugin);
    for (size_t i = 0; i < configured.size(); ++i)
        require(parameterStateMatches(configured[i], prepared[i]), "prepareToPlay changed a configured parameter");
    juce::AudioBuffer<float> audio(2, blockSize);
    juce::MidiBuffer midi;
    std::vector<float> result;
    result.reserve(totalSamples * 2);
    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const auto count = juce::jmin(blockSize, totalSamples - offset);
        audio.setSize(2, count, false, false, true);
        for (int sample = 0; sample < count; ++sample)
        {
            audio.setSample(0, sample, fixtureSample(0, offset + sample));
            audio.setSample(1, sample, fixtureSample(1, offset + sample));
        }
        plugin.processBlock(audio, midi);
        require(finiteSamples(audio), "VST3 emitted a nonfinite audio sample");
        for (int sample = 0; sample < count; ++sample)
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto value = audio.getSample(channel, sample);
                require(std::abs(value) < 2.0f, "Unexpected VST3 output bound");
                result.push_back(value);
            }
    }
    plugin.releaseResources();
    requireWetStereoAudio(result, true);
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
        calibrateParameterStateValidator();
        calibrateWetStereoValidator();
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
        const auto initialDefaults = readParameters(*instance);
        for (const auto& setting : settings)
        {
            auto& value = parameter(*instance, setting.first);
            // The VST3 controller can cache an unsnapped request while APVTS
            // persists its legal grid value. Ask the plugin's own formatter/parser
            // for a canonical request instead of treating that difference as loss.
            const auto text = value.getText(setting.second, 128);
            const auto canonical = value.getValueForText(text);
            require(isNormalizedParameterValue(canonical),
                    "VST3 parameter returned an invalid canonical value");
            if (std::abs(canonical - setting.second) > 1.0e-7f)
                std::fprintf(stderr, "[host-test] canonical %s: request=%g text='%s' value=%g\n",
                             setting.first, static_cast<double>(setting.second), text.toRawUTF8(),
                             static_cast<double>(canonical));
            value.setValueNotifyingHost(canonical);
            const auto saved = value.getValue();
            require(isNormalizedParameterValue(saved), "VST3 parameter returned an invalid saved value");
            requireParameterRequest(canonical, saved, setting.first);
            expectedParameters.push_back(saved);
        }
        requireFreshDefaultsDiffer(expectedParameters, initialDefaults);
        juce::MemoryBlock state;
        instance->getStateInformation(state);
        require(state.getSize() > 0 && state.getSize() <= static_cast<size_t>(std::numeric_limits<int>::max()),
                "VST3 state is empty or too large");
        const auto before = render(*instance);
        instance.reset();
        auto restored = manager.createPluginInstance(*descriptions[0], 48000.0, 256, error);
        require(restored != nullptr, "VST3 recreation failed");
        require(restored->getTotalNumInputChannels() == 2 && restored->getTotalNumOutputChannels() == 2,
                "Recreated VST3 stereo bus layout is incorrect");
        requireFreshDefaultsDiffer(expectedParameters, readParameters(*restored));
        std::fprintf(stderr, "[host-test] actual fresh-default VST3 differs: no-op restore cannot pass\n");
        restored->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        size_t index = 0;
        for (const auto& setting : settings)
        {
            const auto expected = expectedParameters[index++];
            const auto actual = parameter(*restored, setting.first).getValue();
            if (! parameterStateMatches(expected, actual))
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
