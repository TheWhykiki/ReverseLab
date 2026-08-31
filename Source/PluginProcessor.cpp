#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

ReverseLabAudioProcessor::ReverseLabAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "ReverseLabState", rl::params::createLayout())
{
    for (auto& value : scope) value.store(0.0f);
}

bool ReverseLabAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void ReverseLabAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;
    maximumDelay = juce::jmax(4096, static_cast<int>(std::ceil(sampleRate * 32.0)));
    engine.prepare(sampleRate, maximumDelay);
    dryDelay.setSize(2, maximumDelay + 8, false, true, false);
    dryDelay.clear();
    dryWrite = 0;
    filterState = {};
    smoothedBpm = 120.0;
    wasPlaying = false;
    appliedSeed = 0;
    smoothedMix.reset(sampleRate, 0.025);
    smoothedOutput.reset(sampleRate, 0.025);
    smoothedBypass.reset(sampleRate, 0.025);
    smoothedMix.setCurrentAndTargetValue(parameters.getRawParameterValue(rl::params::mix)->load() * 0.01f);
    smoothedOutput.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(parameters.getRawParameterValue(rl::params::output)->load()));
    smoothedBypass.setCurrentAndTargetValue(parameters.getRawParameterValue(rl::params::bypass)->load() > 0.5f ? 1.0f : 0.0f);
}

int ReverseLabAudioProcessor::calculateLengthSamples(int choice, double bpm, bool sync) const noexcept
{
    choice = juce::jlimit(0, static_cast<int>(rl::params::subdivisionBeats.size()) - 1, choice);
    if (sync)
    {
        const auto safeBpm = juce::jlimit(20.0, 400.0, bpm);
        return juce::jlimit(16, maximumDelay - 8,
                            static_cast<int>(std::round(rl::params::subdivisionBeats[(size_t) choice]
                                                        * 60.0 / safeBpm * currentSampleRate)));
    }
    const auto normalized = static_cast<double>(choice) / (rl::params::subdivisionBeats.size() - 1.0);
    const auto ms = 20.0 * std::pow(4000.0 / 20.0, normalized);
    return juce::jlimit(16, maximumDelay - 8, static_cast<int>(std::round(ms * currentSampleRate / 1000.0)));
}

void ReverseLabAudioProcessor::updateLatency(int samples)
{
    samples = juce::jlimit(0, maximumDelay - 8, samples);
    if (samples != displayedLatency.load())
    {
        displayedLatency.store(samples);
        setLatencySamples(samples);
    }
}

float ReverseLabAudioProcessor::processFilters(int channel, float input, float hpHz, float lpHz) noexcept
{
    auto& state = filterState[(size_t) channel];
    const auto lpA = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * lpHz
                                     / static_cast<float>(currentSampleRate));
    state.low += lpA * (input - state.low);
    auto value = state.low;
    const auto hpA = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * hpHz
                                     / static_cast<float>(currentSampleRate));
    state.highLow += hpA * (value - state.highLow);
    return value - state.highLow;
}

void ReverseLabAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto channels = juce::jmin(2, buffer.getNumChannels());
    if (channels == 0) return;

    bool playing = false;
    double hostBpm = 120.0;
    if (auto position = getPlayHead() != nullptr ? getPlayHead()->getPosition() : std::nullopt)
    {
        if (auto bpm = position->getBpm()) hostBpm = *bpm;
        playing = position->getIsPlaying();
    }
    if (std::isfinite(hostBpm) && hostBpm >= 20.0 && hostBpm <= 400.0)
        smoothedBpm += 0.15 * (hostBpm - smoothedBpm);
    if (playing && !wasPlaying) engine.reset();
    wasPlaying = playing;

    const auto sync = parameters.getRawParameterValue(rl::params::sync)->load() > 0.5f;
    const auto linked = parameters.getRawParameterValue(rl::params::link)->load() > 0.5f;
    const auto leftChoice = static_cast<int>(parameters.getRawParameterValue(rl::params::leftSize)->load());
    const auto rightChoice = linked ? leftChoice : static_cast<int>(parameters.getRawParameterValue(rl::params::rightSize)->load());
    const auto leftLength = calculateLengthSamples(leftChoice, smoothedBpm, sync);
    const auto rightLength = calculateLengthSamples(rightChoice, smoothedBpm, sync);
    const auto latency = juce::jmax(leftLength, rightLength);
    updateLatency(latency);

    rl::EngineSettings settings;
    settings.leftLength = leftLength;
    settings.rightLength = rightLength;
    settings.speed = parameters.getRawParameterValue(rl::params::speed)->load();
    settings.crossfade = parameters.getRawParameterValue(rl::params::crossfade)->load() * 0.01f;
    settings.feedback = parameters.getRawParameterValue(rl::params::feedback)->load() * 0.01f;
    settings.stereoOffset = parameters.getRawParameterValue(rl::params::stereoOffset)->load() * 0.01f;
    settings.randomAmount = parameters.getRawParameterValue(rl::params::random)->load() * 0.01f;
    settings.freeze = parameters.getRawParameterValue(rl::params::freeze)->load() > 0.5f;
    settings.retrigger = parameters.getRawParameterValue(rl::params::retrigger)->load() > 0.5f;
    const auto requestedSeed = static_cast<uint32_t>(parameters.getRawParameterValue(rl::params::seed)->load());
    if (requestedSeed != appliedSeed)
    {
        appliedSeed = requestedSeed;
        engine.setSeed(appliedSeed);
    }

    smoothedMix.setTargetValue(parameters.getRawParameterValue(rl::params::mix)->load() * 0.01f);
    smoothedOutput.setTargetValue(juce::Decibels::decibelsToGain(parameters.getRawParameterValue(rl::params::output)->load()));
    smoothedBypass.setTargetValue(parameters.getRawParameterValue(rl::params::bypass)->load() > 0.5f ? 1.0f : 0.0f);
    const auto hp = parameters.getRawParameterValue(rl::params::highpass)->load();
    const auto lp = parameters.getRawParameterValue(rl::params::lowpass)->load();
    const auto delayCapacity = dryDelay.getNumSamples();
    const auto readPos = (dryWrite - latency + delayCapacity) % delayCapacity;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto wetAmount = smoothedMix.getNextValue();
        const auto dryGain = std::cos(wetAmount * juce::MathConstants<float>::halfPi);
        const auto wetGain = std::sin(wetAmount * juce::MathConstants<float>::halfPi);
        const auto outputGain = smoothedOutput.getNextValue();
        const auto bypassAmount = smoothedBypass.getNextValue();
        float scopeValue = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto input = buffer.getSample(channel, sample);
            dryDelay.setSample(channel, dryWrite, input);
            const auto dry = dryDelay.getSample(channel, (readPos + sample) % delayCapacity);
            auto wet = engine.processSample(channel, input, settings);
            wet = processFilters(channel, wet, hp, lp);
            const auto processed = (dry * dryGain + wet * wetGain) * outputGain;
            auto output = processed + bypassAmount * (input - processed);
            if (!std::isfinite(output)) output = 0.0f;
            buffer.setSample(channel, sample, output);
            scopeValue += output / static_cast<float>(channels);
        }
        if (channels == 1)
            (void) engine.processSample(1, buffer.getSample(0, sample), settings);
        engine.advance();
        dryWrite = (dryWrite + 1) % delayCapacity;
        if ((sample & 15) == 0)
        {
            auto index = scopeWrite.load(std::memory_order_relaxed);
            scope[(size_t) index].store(scopeValue, std::memory_order_relaxed);
            scopeWrite.store((index + 1) % static_cast<int>(scope.size()), std::memory_order_release);
        }
    }
}

float ReverseLabAudioProcessor::getScopeSample(int index) const noexcept
{
    index = juce::jlimit(0, static_cast<int>(scope.size()) - 1, index);
    return scope[(size_t) index].load(std::memory_order_relaxed);
}

juce::AudioProcessorEditor* ReverseLabAudioProcessor::createEditor()
{
    return new ReverseLabAudioProcessorEditor(*this);
}

const juce::String ReverseLabAudioProcessor::getProgramName(int index)
{
    static const juce::StringArray names { "Clean Reverse", "Stereo Drift", "Frozen Texture",
                                           "Feedback Rise", "Chopped Eighths", "Wide Triplets" };
    return names[juce::jlimit(0, names.size() - 1, index)];
}

void ReverseLabAudioProcessor::setPlainParameter(const char* id, float plainValue)
{
    if (auto* parameter = parameters.getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
}

void ReverseLabAudioProcessor::setCurrentProgram(int index)
{
    currentProgram = juce::jlimit(0, getNumPrograms() - 1, index);
    setPlainParameter(rl::params::sync, 1.0f); setPlainParameter(rl::params::link, 1.0f);
    setPlainParameter(rl::params::leftSize, 8.0f); setPlainParameter(rl::params::rightSize, 8.0f);
    setPlainParameter(rl::params::speed, 1.0f); setPlainParameter(rl::params::crossfade, 4.0f);
    setPlainParameter(rl::params::mix, 100.0f); setPlainParameter(rl::params::feedback, 0.0f);
    setPlainParameter(rl::params::freeze, 0.0f); setPlainParameter(rl::params::stereoOffset, 0.0f);
    setPlainParameter(rl::params::random, 0.0f); setPlainParameter(rl::params::highpass, 20.0f);
    setPlainParameter(rl::params::lowpass, 20000.0f); setPlainParameter(rl::params::output, 0.0f);
    switch (currentProgram)
    {
        case 1: setPlainParameter(rl::params::link, 0.0f); setPlainParameter(rl::params::rightSize, 9.0f);
                setPlainParameter(rl::params::stereoOffset, 35.0f); setPlainParameter(rl::params::random, 18.0f); break;
        case 2: setPlainParameter(rl::params::freeze, 1.0f); setPlainParameter(rl::params::feedback, 35.0f);
                setPlainParameter(rl::params::lowpass, 6800.0f); break;
        case 3: setPlainParameter(rl::params::feedback, 72.0f); setPlainParameter(rl::params::mix, 76.0f);
                setPlainParameter(rl::params::highpass, 110.0f); break;
        case 4: setPlainParameter(rl::params::leftSize, 5.0f); setPlainParameter(rl::params::rightSize, 5.0f);
                setPlainParameter(rl::params::crossfade, 1.5f); break;
        case 5: setPlainParameter(rl::params::link, 0.0f); setPlainParameter(rl::params::leftSize, 7.0f);
                setPlainParameter(rl::params::rightSize, 10.0f); setPlainParameter(rl::params::stereoOffset, 62.0f); break;
        default: break;
    }
}

void ReverseLabAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();
    state.setProperty("program", currentProgram, nullptr);
    state.setProperty("editorWidth", editorWidth, nullptr);
    state.setProperty("editorHeight", editorHeight, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destination);
}

void ReverseLabAudioProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
    {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid())
        {
            currentProgram = juce::jlimit(0, getNumPrograms() - 1, static_cast<int>(state.getProperty("program", 0)));
            editorWidth = juce::jlimit(720, 1440, static_cast<int>(state.getProperty("editorWidth", 900)));
            editorHeight = juce::jlimit(460, 920, static_cast<int>(state.getProperty("editorHeight", 610)));
            parameters.replaceState(state);
            engine.reset();
            appliedSeed = 0;
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ReverseLabAudioProcessor();
}
