#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryBank.h"
#include <algorithm>
#include <cmath>

ReverseLabAudioProcessor::ReverseLabAudioProcessor(juce::File presetStorage)
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "ReverseLabState", rl::params::createLayout()),
      presets(*this, parameters, "ReverseLab", factoryBank(), std::move(presetStorage))
{
    for (auto& channel : scope)
        for (auto& value : channel)
            value.store(0.0f);
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
    // Sixteen seconds covers every free-time value and two bars down to 30 BPM while
    // keeping multi-instance sessions practical at high sample rates.
    maximumDelay = juce::jmax(4096, static_cast<int>(std::ceil(sampleRate * 16.0)) + 8);
    engine.prepare(sampleRate, maximumDelay);
    dryDelay.setSize(2, maximumDelay + 8, false, true, false);
    wetAlignmentDelay.setSize(2, maximumDelay + 8, false, true, false);
    dryDelay.clear();
    wetAlignmentDelay.clear();
    dryWrite = wetWrite = 0;
    validDelaySamples = 0;
    filterState = {};
    wasPlaying = false;
    previousBlockPosition.reset();
    previousBlockSamples = 0;
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
    smoothedHighpassEnabled.reset(sampleRate, 0.050);
    smoothedLowpassEnabled.reset(sampleRate, 0.050);
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
    smoothedHighpassEnabled.setCurrentAndTargetValue(
        parameters.getRawParameterValue(rl::params::highpass)->load() > 20.01f ? 1.0f : 0.0f);
    smoothedLowpassEnabled.setCurrentAndTargetValue(
        parameters.getRawParameterValue(rl::params::lowpass)->load() < 19999.0f ? 1.0f : 0.0f);
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

void ReverseLabAudioProcessor::releaseResources()
{
    engine.release();
    dryDelay.setSize(0, 0, false, false, false);
    wetAlignmentDelay.setSize(0, 0, false, false, false);
    dryWrite = wetWrite = 0;
    validDelaySamples = 0;
    filterState = {};
    wetTaps = {};
    wasPlaying = false;
    previousBlockPosition.reset();
    previousBlockSamples = 0;
    for (auto& channel : scope)
        for (auto& value : channel)
            value.store(0.0f, std::memory_order_relaxed);
    scopeWrite.store(0, std::memory_order_release);
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
    // Include an old, still-active segment while a length/latency change is in flight.
    const auto segmentSamples = juce::jmax(juce::jmax(left, right),
        juce::jmax(pendingLatency.load(), acknowledgedLatency.load()));
    const auto rate = juce::jmax(1.0, currentSampleRate);
    const auto segmentSeconds = static_cast<double>(segmentSamples) / rate;
    if (parameters.getRawParameterValue(rl::params::freeze)->load() > 0.5f)
        return 3600.0;
    const auto feedback = juce::jlimit(0.0, 0.95,
        static_cast<double>(parameters.getRawParameterValue(rl::params::feedback)->load()) * 0.01);
    const auto speed = juce::jlimit(0.25, 4.0,
        static_cast<double>(parameters.getRawParameterValue(rl::params::speed)->load()));
    // Half of the 0.01%-parameter step ignores normalised zero's float roundoff; it cannot
    // produce even a one-sample offset anywhere in the supported 16-second history.
    const auto shifted = std::abs(parameters.getRawParameterValue(rl::params::stereoOffset)->load()) >= 0.005f
                         || parameters.getRawParameterValue(rl::params::random)->load() >= 0.005f;
    const auto historySeconds = juce::jmax(segmentSeconds, static_cast<double>(maximumDelay + 8) / rate);
    // During reverse playback reader and writer separate at (speed+1). Offset/random can wrap
    // into any older ring frame; an exhausted reader can then hold that frame for a segment.
    const auto readAge = shifted ? historySeconds + segmentSeconds
        : juce::jmin((1.0 + speed) * segmentSeconds + 4.0 / rate, historySeconds + segmentSeconds);
    // A millisecond per pass covers the feedback anti-alias pole; 100 ms covers parameter/filter
    // settling. Extra wet alignment is outside the feedback loop, so add it only once.
    const auto repeats = feedback <= 0.000001 ? 0.0 : std::ceil(std::log(0.0001) / std::log(feedback));
    return (readAge + 0.001) * (1.0 + repeats) + segmentSeconds + 0.1;
}

void ReverseLabAudioProcessor::queueLatencyUpdate(int samples) noexcept
{
    samples = juce::jlimit(0, maximumDelay - 8, samples);
    if (samples != pendingLatency.load(std::memory_order_relaxed))
        pendingLatency.store(samples);
}

void ReverseLabAudioProcessor::timerCallback()
{
    if (const auto requestedProgram = pendingProgramRequest.exchange(-1, std::memory_order_acq_rel);
        requestedProgram >= 0)
        applyProgramChange(requestedProgram);
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
    dryWrite = wetWrite = 0;
    validDelaySamples = 0;
}

float ReverseLabAudioProcessor::processFilters(int channel, float input, float hpHz, float lpHz,
                                               float hpAmount, float lpAmount) noexcept
{
    auto& state = filterState[(size_t) channel];
    const auto rate = static_cast<float>(juce::jmax(1.0, currentSampleRate));
    lpAmount = juce::jlimit(0.0f, 1.0f, lpAmount);
    if (lpAmount > 0.0f)
    {
        const auto lpA = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * lpHz / rate);
        state.low += lpA * (input - state.low);
    }
    else
        state.low = input;
    auto value = input + lpAmount * (state.low - input);

    hpAmount = juce::jlimit(0.0f, 1.0f, hpAmount);
    if (hpAmount > 0.0f)
    {
        const auto hpA = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * hpHz / rate);
        state.highLow += hpA * (value - state.highLow);
    }
    else
        state.highLow = value;
    const auto highPassed = value - state.highLow;
    return value + hpAmount * (highPassed - value);
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
    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
        {
            if (auto bpm = position->getBpm()) hostBpm = *bpm;
            if (auto time = position->getTimeInSamples()) blockPosition = *time;
            if (auto ppq = position->getPpqPosition(); ppq && std::isfinite(*ppq)) ppqPosition = *ppq;
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
    const auto transportDelta = playing && blockPosition && previousBlockPosition
        ? std::abs(static_cast<long double>(*blockPosition)
                   - static_cast<long double>(*previousBlockPosition)
                   - static_cast<long double>(previousBlockSamples))
        : 0.0L;
    const auto transportJump = transportDelta
        > static_cast<long double>(juce::jmax<int64_t>(64, buffer.getNumSamples() * 2));
    if ((playing && !wasPlaying) || transportJump)
    {
        // A frozen texture must survive stop/start and loop cycling: resetting the engine here
        // would discard the capture and the pre-roll would freeze whatever plays next instead.
        if (!engine.isHoldingFrozenCapture())
        {
            engine.reset();
            engine.setSeed(appliedSeed);
        }
        invalidateDelayLines();
        filterState = {};
    }
    wasPlaying = playing;
    previousBlockPosition = playing ? blockPosition : std::nullopt;
    previousBlockSamples = buffer.getNumSamples();

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
    smoothedHighpassEnabled.setTargetValue(
        parameters.getRawParameterValue(rl::params::highpass)->load() > 20.01f ? 1.0f : 0.0f);
    smoothedLowpassEnabled.setTargetValue(
        parameters.getRawParameterValue(rl::params::lowpass)->load() < 19999.0f ? 1.0f : 0.0f);
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
        const auto hpAmount = smoothedHighpassEnabled.getNextValue();
        const auto lpAmount = smoothedLowpassEnabled.getNextValue();
        const auto monoInput = buffer.getSample(0, sample);
        std::array<float, 2> scopeValues {};
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto input = buffer.getSample(channel, sample);
            dryDelay.setSample(channel, dryWrite, input);
            const auto readDelayTap = [this, channel, delayCapacity](int tap) noexcept
            {
                if (tap > validDelaySamples) return 0.0f;
                const auto index = (dryWrite - tap + delayCapacity) % delayCapacity;
                return dryDelay.getSample(channel, index);
            };
            const auto dryNew = readDelayTap(latency);
            const auto dry = latencyTransitionRemaining > 0
                ? readDelayTap(previousProcessingLatency) * oldTapGain + dryNew * newTapGain
                : dryNew;
            auto wet = engine.processSample(channel, input, settings);
            wet = processFilters(channel, wet, hp, lp, hpAmount, lpAmount);
            wetAlignmentDelay.setSample(channel, wetWrite, wet);
            const auto readWetTap = [this, channel, delayCapacity](int offset) noexcept
            {
                if (offset > validDelaySamples) return 0.0f;
                const auto index = (wetWrite - offset + delayCapacity) % delayCapacity;
                return wetAlignmentDelay.getSample(channel, index);
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
            scopeValues[(size_t) channel] = output;
        }
        if (channels == 1)
        {
            (void) engine.processSample(1, monoInput, settings);
            scopeValues[1] = scopeValues[0];
        }
        engine.advance();
        dryWrite = (dryWrite + 1) % delayCapacity;
        wetWrite = (wetWrite + 1) % delayCapacity;
        validDelaySamples = juce::jmin(delayCapacity - 1, validDelaySamples + 1);
        if (latencyTransitionRemaining > 0) --latencyTransitionRemaining;
        if ((sample & 15) == 0)
        {
            auto index = scopeWrite.load(std::memory_order_relaxed);
            for (size_t channel = 0; channel < scope.size(); ++channel)
                scope[channel][(size_t) index].store(scopeValues[channel], std::memory_order_relaxed);
            scopeWrite.store((index + 1) % static_cast<int>(scope[0].size()), std::memory_order_release);
        }
    }
    const auto nextLatency = juce::jmax(engine.getActiveLength(0), engine.getActiveLength(1));
    if (nextLatency > 1)
        queueLatencyUpdate(nextLatency);
}

float ReverseLabAudioProcessor::getScopeSample(int channel, int index) const noexcept
{
    channel = juce::jlimit(0, static_cast<int>(scope.size()) - 1, channel);
    index = juce::jlimit(0, static_cast<int>(scope[0].size()) - 1, index);
    return scope[(size_t) channel][(size_t) index].load(std::memory_order_relaxed);
}

juce::AudioProcessorEditor* ReverseLabAudioProcessor::createEditor()
{
    return new ReverseLabAudioProcessorEditor(*this);
}

int ReverseLabAudioProcessor::getNumPrograms() { return static_cast<int>(factoryBank().size()); }

const juce::String ReverseLabAudioProcessor::getProgramName(int index)
{
    return juce::isPositiveAndBelow(index, getNumPrograms()) ? factoryBank()[static_cast<size_t>(index)].name : juce::String {};
}

void ReverseLabAudioProcessor::setPlainParameter(const char* id, float plainValue)
{
    if (auto* parameter = parameters.getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
}

void ReverseLabAudioProcessor::setCurrentProgram(int index)
{
    const auto program = juce::jlimit(0, getNumPrograms() - 1, index);
    auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
    const auto onMessageThread = messageManager != nullptr && messageManager->isThisTheMessageThread();
    if (onMessageThread && applyingProgramChange
        && program == currentProgram.load(std::memory_order_acquire))
        return;

    pendingProgramRequest.store(program, std::memory_order_release);
    if (onMessageThread && ! applyingProgramChange)
    {
        const auto requestedProgram = pendingProgramRequest.exchange(-1, std::memory_order_acq_rel);
        if (requestedProgram >= 0)
            applyProgramChange(requestedProgram);
    }
}

void ReverseLabAudioProcessor::applyProgramChange(int program)
{
    const juce::ScopedValueSetter<bool> applyingChange(applyingProgramChange, true);
    const auto previousProgram = currentProgram.exchange(program, std::memory_order_acq_rel);
    presets.clearSelection();
    for (const auto& [id, value] : factoryBank()[static_cast<size_t>(program)].values)
        setPlainParameter(id.toRawUTF8(), value);
    if (program != previousProgram)
        updateHostDisplay(ChangeDetails().withProgramChanged(true));
}

void ReverseLabAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();
    presets.appendSelection(state);
    const auto savedSize = getLastEditorSize();
    state.setProperty("program", currentProgram.load(std::memory_order_acquire), nullptr);
    state.setProperty("editorWidth", savedSize.x, nullptr);
    state.setProperty("editorHeight", savedSize.y, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destination);
}

void ReverseLabAudioProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
    {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid() && state.hasType(parameters.state.getType()))
        {
            const auto restoredProgram = juce::jlimit(0, getNumPrograms() - 1,
                                                      static_cast<int>(state.getProperty("program", 0)));
            currentProgram.store(restoredProgram, std::memory_order_release);
            pendingProgramRequest.store(-1, std::memory_order_release);
            const auto restoredWidth = juce::jlimit(
                720, 1440, static_cast<int>(state.getProperty("editorWidth", 900)));
            const auto restoredHeight = juce::jlimit(
                460, 920, static_cast<int>(state.getProperty("editorHeight", 610)));
            setRestoredEditorSize(restoredWidth, restoredHeight);
            parameters.replaceState(state);
            presets.restoreSelection(state);
            processingResetRequested.store(true, std::memory_order_release);
            if (auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
                messageManager != nullptr && messageManager->isThisTheMessageThread())
                if (auto* editor = getActiveEditor())
                {
                    const auto restoredSize = getLastEditorSize();
                    editor->setSize(restoredSize.x, restoredSize.y);
                    acknowledgeRestoredEditorSize(restoredSize.x, restoredSize.y);
                }
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ReverseLabAudioProcessor();
}
