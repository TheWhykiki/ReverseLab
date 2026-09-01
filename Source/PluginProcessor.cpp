#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

ReverseLabAudioProcessor::ReverseLabAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "ReverseLabState", rl::params::createLayout())
{
    for (auto& value : scope) value.store(0.0f);
    startTimerHz(30);
}

ReverseLabAudioProcessor::~ReverseLabAudioProcessor()
{
    stopTimer();
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
    wetAlignmentDelay.setSize(2, maximumDelay + 8, false, true, false);
    dryDelay.clear();
    wetAlignmentDelay.clear();
    for (auto& tags : dryGenerations) tags.assign(static_cast<size_t>(maximumDelay + 8), 0u);
    for (auto& tags : wetGenerations) tags.assign(static_cast<size_t>(maximumDelay + 8), 0u);
    delayGeneration = 1;
    dryWrite = wetWrite = 0;
    filterState = {};
    wasPlaying = false;
    previousBlockPosition.reset();
    appliedSeed = 0;
    retriggerCountdown = -1;
    lastRetriggerParameter = false;
    smoothedMix.reset(sampleRate, 0.025);
    smoothedOutput.reset(sampleRate, 0.025);
    smoothedBypass.reset(sampleRate, 0.025);
    smoothedSpeed.reset(sampleRate, 0.025);
    smoothedCrossfade.reset(sampleRate, 0.025);
    smoothedFeedback.reset(sampleRate, 0.025);
    smoothedHighpass.reset(sampleRate, 0.025);
    smoothedLowpass.reset(sampleRate, 0.025);
    smoothedOffset.reset(sampleRate, 0.025);
    smoothedRandom.reset(sampleRate, 0.025);
    smoothedMix.setCurrentAndTargetValue(parameters.getRawParameterValue(rl::params::mix)->load() * 0.01f);
    smoothedOutput.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(parameters.getRawParameterValue(rl::params::output)->load()));
    smoothedBypass.setCurrentAndTargetValue(parameters.getRawParameterValue(rl::params::bypass)->load() > 0.5f ? 1.0f : 0.0f);
    smoothedSpeed.setCurrentAndTargetValue(parameters.getRawParameterValue(rl::params::speed)->load());
    smoothedCrossfade.setCurrentAndTargetValue(parameters.getRawParameterValue(rl::params::crossfade)->load() * 0.01f);
    smoothedFeedback.setCurrentAndTargetValue(parameters.getRawParameterValue(rl::params::feedback)->load() * 0.01f);
    smoothedHighpass.setCurrentAndTargetValue(parameters.getRawParameterValue(rl::params::highpass)->load());
    smoothedLowpass.setCurrentAndTargetValue(parameters.getRawParameterValue(rl::params::lowpass)->load());
    smoothedOffset.setCurrentAndTargetValue(parameters.getRawParameterValue(rl::params::stereoOffset)->load() * 0.01f);
    smoothedRandom.setCurrentAndTargetValue(parameters.getRawParameterValue(rl::params::random)->load() * 0.01f);
    // Use the host tempo and meter when they are already known so the latency reported from
    // prepareToPlay() matches the first processed segment instead of a 120 BPM 4/4 assumption.
    double initialBpm = 120.0, initialBeatsPerBar = 4.0;
    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
        {
            if (auto bpm = position->getBpm())
                if (std::isfinite(*bpm) && *bpm >= 20.0 && *bpm <= 400.0) initialBpm = *bpm;
            if (auto signature = position->getTimeSignature())
                if (signature->numerator > 0 && signature->denominator > 0)
                    initialBeatsPerBar = static_cast<double>(signature->numerator) * 4.0
                                         / static_cast<double>(signature->denominator);
        }
    smoothedBpm = initialBpm;
    publishedBpm.store(initialBpm);
    publishedBeatsPerBar.store(initialBeatsPerBar);
    const auto sync = parameters.getRawParameterValue(rl::params::sync)->load() > 0.5f;
    const auto linked = parameters.getRawParameterValue(rl::params::link)->load() > 0.5f;
    const auto left = calculateLengthSamples(static_cast<int>(parameters.getRawParameterValue(rl::params::leftSize)->load()),
                                             parameters.getRawParameterValue(rl::params::leftFreeMs)->load(), initialBpm, initialBeatsPerBar, sync);
    const auto right = linked ? left : calculateLengthSamples(
        static_cast<int>(parameters.getRawParameterValue(rl::params::rightSize)->load()),
        parameters.getRawParameterValue(rl::params::rightFreeMs)->load(), initialBpm, initialBeatsPerBar, sync);
    activeProcessingLatency = juce::jmax(left, right);
    wetTaps = {};
    wetTaps[0].current = juce::jmax(0, activeProcessingLatency - left);
    wetTaps[1].current = juce::jmax(0, activeProcessingLatency - right);
    previousProcessingLatency = activeProcessingLatency;
    latencyTransitionRemaining = 0;
    latencyTransitionLength = juce::jmax(1, static_cast<int>(std::round(sampleRate * 0.01)));
    displayedLatency.store(activeProcessingLatency);
    pendingLatency.store(activeProcessingLatency);
    acknowledgedLatency.store(activeProcessingLatency);
    setLatencySamples(activeProcessingLatency);
}

int ReverseLabAudioProcessor::calculateLengthSamples(int choice, float freeMs, double bpm,
                                                     double beatsPerBar, bool sync) const noexcept
{
    const auto safeMaximum = juce::jmax(16, maximumDelay - 8);
    choice = juce::jlimit(0, static_cast<int>(rl::params::subdivisionBeats.size()) - 1, choice);
    if (sync)
    {
        const auto safeBpm = juce::jlimit(20.0, 400.0, bpm);
        const auto beatLength = choice >= 13
            ? juce::jlimit(1.0, 32.0, beatsPerBar) * static_cast<double>(choice == 14 ? 2 : 1)
            : rl::params::subdivisionBeats[(size_t) choice];
        return juce::jlimit(16, safeMaximum,
                            static_cast<int>(std::round(beatLength * 60.0 / safeBpm * currentSampleRate)));
    }
    const auto ms = juce::jlimit(20.0f, 4000.0f, freeMs);
    return juce::jlimit(16, safeMaximum, static_cast<int>(std::round(ms * currentSampleRate / 1000.0f)));
}

double ReverseLabAudioProcessor::getTailLengthSeconds() const
{
    const auto sync = parameters.getRawParameterValue(rl::params::sync)->load() > 0.5f;
    const auto linked = parameters.getRawParameterValue(rl::params::link)->load() > 0.5f;
    const auto left = calculateLengthSamples(
        static_cast<int>(parameters.getRawParameterValue(rl::params::leftSize)->load()),
        parameters.getRawParameterValue(rl::params::leftFreeMs)->load(), publishedBpm.load(),
        publishedBeatsPerBar.load(), sync);
    const auto right = linked ? left : calculateLengthSamples(
        static_cast<int>(parameters.getRawParameterValue(rl::params::rightSize)->load()),
        parameters.getRawParameterValue(rl::params::rightFreeMs)->load(), publishedBpm.load(),
        publishedBeatsPerBar.load(), sync);
    const auto segmentSeconds = static_cast<double>(juce::jmax(left, right))
                                / juce::jmax(1.0, currentSampleRate);
    if (parameters.getRawParameterValue(rl::params::freeze)->load() > 0.5f)
        return 3600.0;
    const auto feedback = juce::jlimit(0.0, 0.95,
        static_cast<double>(parameters.getRawParameterValue(rl::params::feedback)->load()) * 0.01);
    if (feedback <= 0.000001)
        return segmentSeconds;
    const auto repeatsToMinus60dB = std::ceil(std::log(0.001) / std::log(feedback));
    return juce::jlimit(segmentSeconds, 3600.0, segmentSeconds * (1.0 + repeatsToMinus60dB));
}

void ReverseLabAudioProcessor::queueLatencyUpdate(int samples) noexcept
{
    samples = juce::jlimit(0, maximumDelay - 8, samples);
    if (samples != pendingLatency.load(std::memory_order_relaxed))
        pendingLatency.store(samples);
}

void ReverseLabAudioProcessor::timerCallback()
{
    if (programChangeRequested.exchange(false, std::memory_order_acq_rel))
        applyPendingProgramChange();
    const auto latency = pendingLatency.load();
    if (latency != getLatencySamples())
    {
        setLatencySamples(latency);
        acknowledgedLatency.store(latency, std::memory_order_release);
        displayedLatency.store(latency, std::memory_order_release);
    }
    if (retriggerResetRequested.exchange(false))
        if (auto* parameter = parameters.getParameter(rl::params::retrigger))
            parameter->setValueNotifyingHost(0.0f);
}

void ReverseLabAudioProcessor::invalidateDelayLines() noexcept
{
    if (++delayGeneration == 0)
    {
        for (auto& tags : dryGenerations) std::fill(tags.begin(), tags.end(), 0u);
        for (auto& tags : wetGenerations) std::fill(tags.begin(), tags.end(), 0u);
        delayGeneration = 1;
    }
    dryWrite = wetWrite = 0;
}

float ReverseLabAudioProcessor::processFilters(int channel, float input, float hpHz, float lpHz) noexcept
{
    auto& state = filterState[(size_t) channel];
    auto value = input;
    if (lpHz < 19999.0f)
    {
        const auto lpA = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * lpHz
                                         / static_cast<float>(currentSampleRate));
        state.low += lpA * (input - state.low);
        value = state.low;
    }
    else
        state.low = input;

    if (hpHz > 20.01f)
    {
        const auto hpA = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * hpHz
                                         / static_cast<float>(currentSampleRate));
        state.highLow += hpA * (value - state.highLow);
        value -= state.highLow;
    }
    else
        state.highLow = value;
    return value;
}

void ReverseLabAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto channels = juce::jmin(2, buffer.getNumChannels());
    if (channels == 0) return;
    if (dryDelay.getNumSamples() == 0) return; // processBlock() before prepareToPlay(): pass audio through

    if (processingResetRequested.exchange(false, std::memory_order_acq_rel))
    {
        engine.reset();
        invalidateDelayLines();
        filterState = {};
        appliedSeed = 0;
        retriggerCountdown = -1;
    }

    const auto hostAcknowledgedLatency = acknowledgedLatency.load(std::memory_order_acquire);
    if (hostAcknowledgedLatency != activeProcessingLatency)
    {
        previousProcessingLatency = activeProcessingLatency;
        activeProcessingLatency = hostAcknowledgedLatency;
        latencyTransitionRemaining = latencyTransitionLength;
    }

    bool playing = false;
    double hostBpm = 120.0;
    std::optional<int64_t> blockPosition;
    std::optional<double> ppqPosition;
    double beatsPerBar = 4.0;
    if (auto position = getPlayHead() != nullptr ? getPlayHead()->getPosition() : std::nullopt)
    {
        if (auto bpm = position->getBpm()) hostBpm = *bpm;
        if (auto time = position->getTimeInSamples()) blockPosition = *time;
        if (auto ppq = position->getPpqPosition()) ppqPosition = *ppq;
        if (auto signature = position->getTimeSignature())
            if (signature->numerator > 0 && signature->denominator > 0)
                beatsPerBar = static_cast<double>(signature->numerator) * 4.0
                              / static_cast<double>(signature->denominator);
        playing = position->getIsPlaying();
    }
    if (std::isfinite(hostBpm) && hostBpm >= 20.0 && hostBpm <= 400.0)
    {
        // ~65 ms tempo smoothing expressed in time, so the response does not depend on block size.
        const auto tempoCoefficient = 1.0 - std::exp(-static_cast<double>(buffer.getNumSamples())
                                                     / (currentSampleRate * 0.065));
        smoothedBpm += tempoCoefficient * (hostBpm - smoothedBpm);
    }
    publishedBpm.store(smoothedBpm, std::memory_order_relaxed);
    publishedBeatsPerBar.store(beatsPerBar, std::memory_order_relaxed);
    const auto transportJump = playing && blockPosition && previousBlockPosition
        && std::abs(*blockPosition - (*previousBlockPosition + buffer.getNumSamples()))
               > juce::jmax<int64_t>(64, buffer.getNumSamples() * 2);
    if ((playing && !wasPlaying) || transportJump)
    {
        engine.reset();
        engine.setSeed(appliedSeed);
        invalidateDelayLines();
        filterState = {};
    }
    wasPlaying = playing;
    previousBlockPosition = playing ? blockPosition : std::nullopt;

    const auto sync = parameters.getRawParameterValue(rl::params::sync)->load() > 0.5f;
    const auto linked = parameters.getRawParameterValue(rl::params::link)->load() > 0.5f;
    const auto leftChoice = static_cast<int>(parameters.getRawParameterValue(rl::params::leftSize)->load());
    const auto rightChoice = linked ? leftChoice : static_cast<int>(parameters.getRawParameterValue(rl::params::rightSize)->load());
    const auto leftLength = calculateLengthSamples(leftChoice,
        parameters.getRawParameterValue(rl::params::leftFreeMs)->load(), smoothedBpm, beatsPerBar, sync);
    const auto rightLength = linked ? leftLength : calculateLengthSamples(rightChoice,
        parameters.getRawParameterValue(rl::params::rightFreeMs)->load(), smoothedBpm, beatsPerBar, sync);
    const auto latency = activeProcessingLatency;

    rl::EngineSettings settings;
    settings.leftLength = leftLength;
    settings.rightLength = rightLength;
    settings.freeze = parameters.getRawParameterValue(rl::params::freeze)->load() > 0.5f;
    const auto retriggerParameter = parameters.getRawParameterValue(rl::params::retrigger)->load() > 0.5f;
    if (retriggerParameter && !lastRetriggerParameter)
    {
        retriggerCountdown = 0;
        if (sync && playing && ppqPosition)
        {
            constexpr double gridBeats = 0.125;
            const auto nextGrid = std::ceil((*ppqPosition + 1.0e-9) / gridBeats) * gridBeats;
            const auto beatsUntilGrid = juce::jmax(0.0, nextGrid - *ppqPosition);
            retriggerCountdown = static_cast<int>(std::round(beatsUntilGrid * 60.0 / smoothedBpm * currentSampleRate));
        }
        retriggerResetRequested.store(true);
    }
    lastRetriggerParameter = retriggerParameter;
    const auto requestedSeed = static_cast<uint32_t>(parameters.getRawParameterValue(rl::params::seed)->load());
    if (requestedSeed != appliedSeed)
    {
        appliedSeed = requestedSeed;
        engine.setSeed(appliedSeed);
    }

    smoothedMix.setTargetValue(parameters.getRawParameterValue(rl::params::mix)->load() * 0.01f);
    smoothedOutput.setTargetValue(juce::Decibels::decibelsToGain(parameters.getRawParameterValue(rl::params::output)->load()));
    smoothedBypass.setTargetValue(parameters.getRawParameterValue(rl::params::bypass)->load() > 0.5f ? 1.0f : 0.0f);
    smoothedSpeed.setTargetValue(parameters.getRawParameterValue(rl::params::speed)->load());
    smoothedCrossfade.setTargetValue(parameters.getRawParameterValue(rl::params::crossfade)->load() * 0.01f);
    smoothedFeedback.setTargetValue(parameters.getRawParameterValue(rl::params::feedback)->load() * 0.01f);
    smoothedHighpass.setTargetValue(parameters.getRawParameterValue(rl::params::highpass)->load());
    smoothedLowpass.setTargetValue(parameters.getRawParameterValue(rl::params::lowpass)->load());
    smoothedOffset.setTargetValue(parameters.getRawParameterValue(rl::params::stereoOffset)->load() * 0.01f);
    smoothedRandom.setTargetValue(parameters.getRawParameterValue(rl::params::random)->load() * 0.01f);
    const auto delayCapacity = dryDelay.getNumSamples();

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto transition = latencyTransitionRemaining > 0
            ? 1.0f - static_cast<float>(latencyTransitionRemaining) / static_cast<float>(latencyTransitionLength)
            : 1.0f;
        const auto oldTapGain = std::cos(transition * juce::MathConstants<float>::halfPi);
        const auto newTapGain = std::sin(transition * juce::MathConstants<float>::halfPi);
        const auto wetAmount = smoothedMix.getNextValue();
        const auto dryGain = std::cos(wetAmount * juce::MathConstants<float>::halfPi);
        const auto wetGain = std::sin(wetAmount * juce::MathConstants<float>::halfPi);
        const auto outputGain = smoothedOutput.getNextValue();
        const auto bypassAmount = smoothedBypass.getNextValue();
        settings.speed = smoothedSpeed.getNextValue();
        settings.crossfade = smoothedCrossfade.getNextValue();
        settings.feedback = smoothedFeedback.getNextValue();
        settings.stereoOffset = smoothedOffset.getNextValue();
        settings.randomAmount = smoothedRandom.getNextValue();
        settings.retrigger = retriggerCountdown == 0;
        if (retriggerCountdown >= 0) --retriggerCountdown;
        const auto hp = smoothedHighpass.getNextValue();
        const auto lp = smoothedLowpass.getNextValue();
        const auto monoInput = buffer.getSample(0, sample);
        float scopeValue = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto input = buffer.getSample(channel, sample);
            dryDelay.setSample(channel, dryWrite, input);
            dryGenerations[(size_t) channel][(size_t) dryWrite] = delayGeneration;
            const auto readDelayTap = [this, channel, delayCapacity](int tap) noexcept
            {
                const auto index = (dryWrite - tap + delayCapacity) % delayCapacity;
                return dryGenerations[(size_t) channel][(size_t) index] == delayGeneration
                    ? dryDelay.getSample(channel, index) : 0.0f;
            };
            const auto dryNew = readDelayTap(latency);
            const auto dry = latencyTransitionRemaining > 0
                ? readDelayTap(previousProcessingLatency) * oldTapGain + dryNew * newTapGain
                : dryNew;
            auto wet = engine.processSample(channel, input, settings);
            wet = processFilters(channel, wet, hp, lp);
            wetAlignmentDelay.setSample(channel, wetWrite, wet);
            wetGenerations[(size_t) channel][(size_t) wetWrite] = delayGeneration;
            const auto readWetTap = [this, channel, delayCapacity](int offset) noexcept
            {
                const auto index = (wetWrite - offset + delayCapacity) % delayCapacity;
                return wetGenerations[(size_t) channel][(size_t) index] == delayGeneration
                    ? wetAlignmentDelay.getSample(channel, index) : 0.0f;
            };
            auto& tap = wetTaps[(size_t) channel];
            const auto desiredOffset = juce::jmax(0, latency - engine.getActiveLength(channel));
            if (desiredOffset != tap.current)
            {
                tap.previous = tap.current;
                tap.current = desiredOffset;
                tap.transitionRemaining = latencyTransitionLength;
            }
            wet = readWetTap(tap.current);
            if (tap.transitionRemaining > 0)
            {
                const auto tapTransition = 1.0f - static_cast<float>(tap.transitionRemaining)
                                                  / static_cast<float>(latencyTransitionLength);
                wet = readWetTap(tap.previous) * std::cos(tapTransition * juce::MathConstants<float>::halfPi)
                      + wet * std::sin(tapTransition * juce::MathConstants<float>::halfPi);
                --tap.transitionRemaining;
            }
            const auto processed = (dry * dryGain + wet * wetGain) * outputGain;
            auto output = processed + bypassAmount * (dry - processed);
            if (!std::isfinite(output)) output = 0.0f;
            buffer.setSample(channel, sample, output);
            scopeValue += output / static_cast<float>(channels);
        }
        if (channels == 1)
            (void) engine.processSample(1, monoInput, settings);
        engine.advance();
        dryWrite = (dryWrite + 1) % delayCapacity;
        wetWrite = (wetWrite + 1) % delayCapacity;
        if (latencyTransitionRemaining > 0) --latencyTransitionRemaining;
        if ((sample & 15) == 0)
        {
            auto index = scopeWrite.load(std::memory_order_relaxed);
            scope[(size_t) index].store(scopeValue, std::memory_order_relaxed);
            scopeWrite.store((index + 1) % static_cast<int>(scope.size()), std::memory_order_release);
        }
    }
    const auto nextLatency = juce::jmax(engine.getActiveLength(0), engine.getActiveLength(1));
    if (nextLatency > 1)
        queueLatencyUpdate(nextLatency);
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
    pendingProgram.store(juce::jlimit(0, getNumPrograms() - 1, index), std::memory_order_release);
    programChangeRequested.store(true, std::memory_order_release);
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
        && juce::MessageManager::getInstanceWithoutCreating()->isThisTheMessageThread())
    {
        programChangeRequested.store(false, std::memory_order_release);
        applyPendingProgramChange();
    }
}

void ReverseLabAudioProcessor::applyPendingProgramChange()
{
    const auto program = pendingProgram.load(std::memory_order_acquire);
    currentProgram.store(program, std::memory_order_release);
    setPlainParameter(rl::params::sync, 1.0f); setPlainParameter(rl::params::link, 1.0f);
    setPlainParameter(rl::params::leftSize, 8.0f); setPlainParameter(rl::params::rightSize, 8.0f);
    setPlainParameter(rl::params::leftFreeMs, 500.0f); setPlainParameter(rl::params::rightFreeMs, 500.0f);
    setPlainParameter(rl::params::speed, 1.0f); setPlainParameter(rl::params::crossfade, 4.0f);
    setPlainParameter(rl::params::mix, 100.0f); setPlainParameter(rl::params::feedback, 0.0f);
    setPlainParameter(rl::params::freeze, 0.0f); setPlainParameter(rl::params::stereoOffset, 0.0f);
    setPlainParameter(rl::params::random, 0.0f); setPlainParameter(rl::params::highpass, 20.0f);
    setPlainParameter(rl::params::lowpass, 20000.0f); setPlainParameter(rl::params::output, 0.0f);
    setPlainParameter(rl::params::retrigger, 0.0f); setPlainParameter(rl::params::bypass, 0.0f);
    setPlainParameter(rl::params::seed, 4242.0f);
    switch (program)
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
    state.setProperty("program", currentProgram.load(std::memory_order_acquire), nullptr);
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
            const auto restoredProgram = juce::jlimit(0, getNumPrograms() - 1,
                                                      static_cast<int>(state.getProperty("program", 0)));
            currentProgram.store(restoredProgram, std::memory_order_release);
            pendingProgram.store(restoredProgram, std::memory_order_release);
            programChangeRequested.store(false, std::memory_order_release);
            editorWidth = juce::jlimit(720, 1440, static_cast<int>(state.getProperty("editorWidth", 900)));
            editorHeight = juce::jlimit(460, 920, static_cast<int>(state.getProperty("editorHeight", 610)));
            parameters.replaceState(state);
            processingResetRequested.store(true, std::memory_order_release);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ReverseLabAudioProcessor();
}
