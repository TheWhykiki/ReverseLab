#include <juce_core/juce_core.h>
#include "PluginProcessor.h"
#include "ReverseEngine.h"
#include <algorithm>
#include <cmath>

namespace
{
void setParameter(ReverseLabAudioProcessor& processor, const char* id, float value)
{
    if (auto* parameter = processor.parameters.getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

class TestPlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override { return position; }
    PositionInfo position;
};

// Click detector: tracks the largest sample-to-sample step of a signal. For a sine of amplitude A and
// angular frequency w read at up to `speed` times real time, a clean signal never exceeds A * w * speed.
struct ClickDetector
{
    float previous = 0.0f;
    float maxDelta = 0.0f;
    void push(float value) noexcept { maxDelta = juce::jmax(maxDelta, std::abs(value - previous)); previous = value; }
    static float threshold(float amplitude, float angularFrequency, float maxSpeed) noexcept
    {
        return amplitude * angularFrequency * maxSpeed * 3.0f; // 3x headroom for crossfades and interpolation
    }
};
}

class ReverseEngineTests final : public juce::UnitTest
{
public:
    ReverseEngineTests() : UnitTest("ReverseEngine", "DSP") {}

    void runTest() override
    {
        beginTest("Reverse chunks remain finite at maximum feedback");
        rl::ReverseEngine engine;
        engine.prepare(48000.0, 48000);
        rl::EngineSettings settings;
        settings.leftLength = settings.rightLength = 64;
        settings.feedback = 0.95f;
        settings.crossfade = 0.04f;
        for (int i = 0; i < 100000; ++i)
        {
            const auto input = i == 0 ? 1.0f : 0.0f;
            const auto left = engine.processSample(0, input, settings);
            const auto right = engine.processSample(1, input, settings);
            expect(std::isfinite(left) && std::isfinite(right));
            expectWithinAbsoluteError(left, juce::jlimit(-4.0f, 4.0f, left), 0.0001f);
            engine.advance();
        }

        beginTest("A captured ramp is read in descending order");
        engine.reset();
        settings.feedback = 0.0f;
        settings.crossfade = 0.0f;
        settings.speed = 1.0f;
        std::array<float, 192> output {};
        for (int i = 0; i < 192; ++i)
        {
            output[(size_t) i] = engine.processSample(0, static_cast<float>(i) / 192.0f, settings);
            (void) engine.processSample(1, 0.0f, settings);
            engine.advance();
        }
        int descending = 0;
        for (int i = 130; i < 190; ++i)
            if (output[(size_t) i] < output[(size_t) (i - 1)]) ++descending;
        expect(descending > 50, "Most samples in a complete reverse segment should descend");

        beginTest("Freeze stops replacing the captured material");
        engine.reset();
        settings.freeze = false;
        for (int i = 0; i < 128; ++i)
        {
            (void) engine.processSample(0, 0.25f, settings);
            (void) engine.processSample(1, 0.25f, settings);
            engine.advance();
        }
        settings.freeze = true;
        float energy = 0.0f;
        for (int i = 0; i < 256; ++i)
        {
            energy += std::abs(engine.processSample(0, 0.0f, settings));
            (void) engine.processSample(1, 0.0f, settings);
            engine.advance();
        }
        expect(energy > 1.0f);

        beginTest("Seeded randomisation is deterministic");
        auto render = [](uint32_t seed)
        {
            rl::ReverseEngine e; e.prepare(48000.0, 8192); e.setSeed(seed);
            rl::EngineSettings s; s.leftLength = s.rightLength = 80; s.randomAmount = 0.8f;
            std::array<float, 400> result {};
            for (int i = 0; i < 400; ++i)
            {
                result[(size_t) i] = e.processSample(0, std::sin(i * 0.1f), s);
                (void) e.processSample(1, 0.0f, s); e.advance();
            }
            return result;
        };
        const auto a = render(12345), b = render(12345), c = render(54321);
        expect(a == b);
        expect(a != c);

        beginTest("Supported sample rates and speed extremes remain stable");
        for (const auto sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            for (const auto speed : { 0.25f, 1.0f, 4.0f })
            {
                rl::ReverseEngine stress;
                stress.prepare(sampleRate, static_cast<int>(sampleRate * 4.0));
                rl::EngineSettings s;
                s.leftLength = static_cast<int>(sampleRate * 0.125);
                s.rightLength = static_cast<int>(sampleRate * 0.1875);
                s.speed = speed;
                s.crossfade = 0.25f;
                s.feedback = 0.95f;
                s.randomAmount = 1.0f;
                s.stereoOffset = 1.0f;
                for (int i = 0; i < 32768; ++i)
                {
                    const auto input = std::sin(static_cast<float>(i) * 0.071f) * 0.2f;
                    expect(std::isfinite(stress.processSample(0, input, s)));
                    expect(std::isfinite(stress.processSample(1, -input, s)));
                    stress.advance();
                }
            }
        }

        beginTest("Normalized playback phase stays within display bounds");
        engine.reset();
        settings.leftLength = settings.rightLength = 97;
        settings.feedback = 0.0f;
        settings.freeze = false;
        for (int i = 0; i < 1000; ++i)
        {
            (void) engine.processSample(0, 0.0f, settings);
            (void) engine.processSample(1, 0.0f, settings);
            expect(engine.getNormalizedPhase(0) >= 0.0f && engine.getNormalizedPhase(0) <= 1.0f);
            expect(engine.getNormalizedPhase(1) >= 0.0f && engine.getNormalizedPhase(1) <= 1.0f);
            engine.advance();
        }

        beginTest("Reset invalidates captured audio without exposing stale samples");
        engine.reset();
        settings.leftLength = settings.rightLength = 64;
        settings.freeze = false;
        for (int i = 0; i < 128; ++i)
        {
            (void) engine.processSample(0, 0.75f, settings);
            (void) engine.processSample(1, -0.75f, settings);
            engine.advance();
        }
        engine.reset();
        float resetEnergy = 0.0f;
        for (int i = 0; i < 64; ++i)
        {
            resetEnergy += std::abs(engine.processSample(0, 0.0f, settings));
            resetEnergy += std::abs(engine.processSample(1, 0.0f, settings));
            engine.advance();
        }
        expectWithinAbsoluteError(resetEnergy, 0.0f, 0.0001f);

        beginTest("Speed automation changes read velocity, not read position (no clicks)");
        {
            rl::ReverseEngine speedEngine;
            speedEngine.prepare(48000.0, 48000);
            rl::EngineSettings s;
            s.leftLength = s.rightLength = 24000;
            s.crossfade = 0.04f;
            constexpr float amplitude = 0.5f;
            constexpr float w = juce::MathConstants<float>::twoPi * 500.0f / 48000.0f;
            ClickDetector detector;
            int n = 0;
            auto step = [&](float speed, bool measure)
            {
                s.speed = speed;
                const auto in = amplitude * std::sin(w * static_cast<float>(n++));
                const auto y = speedEngine.processSample(0, in, s);
                (void) speedEngine.processSample(1, in, s);
                speedEngine.advance();
                if (measure) detector.push(y); else detector.previous = y;
            };
            for (int i = 0; i < 44000; ++i) step(1.0f, false);                                  // silent first segment + 20k samples into the second
            for (int i = 0; i < 1200; ++i) step(1.0f + static_cast<float>(i + 1) / 1200.0f, true); // 25 ms ramp 1x -> 2x, like the processor smoother
            for (int i = 0; i < 3000; ++i) step(2.0f, true);                                    // hold across a segment boundary
            expectLessThan(detector.maxDelta, ClickDetector::threshold(amplitude, w, 2.0f),
                           "Speed ramp late in a segment must not scrub the read head");
        }

        beginTest("Maximum-length 4x playback never wraps through the live writer");
        {
            rl::ReverseEngine longEngine;
            constexpr int capacity = 4096;
            constexpr int length = capacity - 8;
            longEngine.prepare(48000.0, length);
            rl::EngineSettings s;
            s.leftLength = s.rightLength = length;
            s.speed = 4.0f;
            s.crossfade = 0.04f;
            float heldMinimum = 10.0f, heldMaximum = -10.0f;
            for (int n = 0; n < length + 1800; ++n)
            {
                const auto input = 0.5f * std::sin(0.071f * static_cast<float>(n));
                const auto longWet = longEngine.processSample(0, input, s);
                (void) longEngine.processSample(1, input, s);
                longEngine.advance();
                if (n >= length + 1300)
                {
                    heldMinimum = juce::jmin(heldMinimum, longWet);
                    heldMaximum = juce::jmax(heldMaximum, longWet);
                }
            }
            expectLessThan(heldMaximum - heldMinimum, 0.001f,
                           "Exhausted history must hold instead of reading newly written samples");
        }

        beginTest("Unfreezing a long capture resumes reverse motion");
        {
            rl::ReverseEngine freezeEngine;
            constexpr int length = 4088;
            freezeEngine.prepare(48000.0, length);
            rl::EngineSettings s;
            s.leftLength = s.rightLength = length;
            s.speed = 1.0f;
            s.crossfade = 0.04f;
            for (int n = 0; n < length * 2; ++n)
            {
                const auto input = 0.5f * std::sin(0.071f * static_cast<float>(n));
                (void) freezeEngine.processSample(0, input, s);
                (void) freezeEngine.processSample(1, input, s);
                freezeEngine.advance();
            }
            s.freeze = true;
            for (int n = 0; n < 3000; ++n)
            {
                (void) freezeEngine.processSample(0, 0.0f, s);
                (void) freezeEngine.processSample(1, 0.0f, s);
                freezeEngine.advance();
            }
            s.freeze = false;
            float minimum = 10.0f, maximum = -10.0f;
            for (int n = 0; n < 256; ++n)
            {
                const auto unfrozenWet = freezeEngine.processSample(0, 0.0f, s);
                (void) freezeEngine.processSample(1, 0.0f, s);
                freezeEngine.advance();
                minimum = juce::jmin(minimum, unfrozenWet);
                maximum = juce::jmax(maximum, unfrozenWet);
            }
            expect(maximum - minimum > 0.05f,
                   "Unfreeze must resume motion instead of holding a DC value");
        }
    }
};

class ProcessorTests final : public juce::UnitTest
{
public:
    ProcessorTests() : UnitTest("ReverseLab Processor", "Integration") {}

    void runTest() override
    {
        beginTest("Tail query is valid before prepareToPlay");
        ReverseLabAudioProcessor unprepared;
        expect(std::isfinite(unprepared.getTailLengthSeconds()));
        expect(unprepared.getTailLengthSeconds() > 0.0);

        beginTest("Free timing is continuous and determines reported latency");
        ReverseLabAudioProcessor processor;
        setParameter(processor, rl::params::sync, 0.0f);
        setParameter(processor, rl::params::link, 0.0f);
        setParameter(processor, rl::params::leftFreeMs, 20.0f);
        setParameter(processor, rl::params::rightFreeMs, 137.5f);
        processor.prepareToPlay(48000.0, 256);
        expectEquals(processor.getLatencySamples(), 6600);
        expectEquals(processor.getCurrentLatencySamples(), 6600);
        expect(processor.getBypassParameter() == processor.parameters.getParameter(rl::params::bypass),
               "The VST3 host bypass must use ReverseLab's latency-aligned bypass parameter");

        beginTest("Tempo-sync history is capped at sixteen seconds");
        {
            ReverseLabAudioProcessor slowTempo;
            setParameter(slowTempo, rl::params::sync, 1.0f);
            setParameter(slowTempo, rl::params::link, 1.0f);
            setParameter(slowTempo, rl::params::leftSize, 14.0f); // 2 Bars
            TestPlayHead slowPlayHead;
            slowPlayHead.position.setBpm(20.0);
            slowPlayHead.position.setTimeSignature(juce::AudioPlayHead::TimeSignature { 4, 4 });
            slowTempo.setPlayHead(&slowPlayHead);
            slowTempo.prepareToPlay(48000.0, 256);
            expectEquals(slowTempo.getLatencySamples(), 16 * 48000,
                         "Very slow two-bar segments must respect the bounded history allocation");
            slowTempo.setPlayHead(nullptr);
        }

        beginTest("A reverted length cancels a pending latency increase");
        {
            ReverseLabAudioProcessor latencyRevert;
            setParameter(latencyRevert, rl::params::sync, 0.0f);
            setParameter(latencyRevert, rl::params::link, 1.0f);
            setParameter(latencyRevert, rl::params::leftFreeMs, 100.0f);
            latencyRevert.prepareToPlay(48000.0, 256);
            juce::AudioBuffer<float> latencyBlock(2, 256);
            juce::MidiBuffer latencyMidi;
            setParameter(latencyRevert, rl::params::leftFreeMs, 300.0f);
            latencyBlock.clear();
            latencyRevert.processBlock(latencyBlock, latencyMidi);
            setParameter(latencyRevert, rl::params::leftFreeMs, 100.0f);
            for (int blockIndex = 0; blockIndex < 64; ++blockIndex)
            {
                latencyBlock.clear();
                latencyRevert.processBlock(latencyBlock, latencyMidi);
            }
            latencyRevert.servicePendingHostUpdatesForTesting();
            expectEquals(latencyRevert.getLatencySamples(), 4800,
                         "The reverted engine length must replace the stale pending latency");
        }

        beginTest("Factory programs are applied by the message-thread service");
        ReverseLabAudioProcessor presetProcessor;
        presetProcessor.setCurrentProgram(3);
        presetProcessor.servicePendingHostUpdatesForTesting();
        expectEquals(presetProcessor.getCurrentProgram(), 3);
        expectWithinAbsoluteError(presetProcessor.parameters.getRawParameterValue(rl::params::feedback)->load(),
                                  72.0f, 0.001f);
        expectWithinAbsoluteError(presetProcessor.parameters.getRawParameterValue(rl::params::bypass)->load(),
                                  0.0f, 0.001f);
        presetProcessor.setCurrentProgram(2);
        presetProcessor.servicePendingHostUpdatesForTesting();
        expectWithinAbsoluteError(presetProcessor.parameters.getRawParameterValue(rl::params::freeze)->load(),
                                  1.0f, 0.001f);
        expectWithinAbsoluteError(presetProcessor.parameters.getRawParameterValue(rl::params::feedback)->load(),
                                  0.0f, 0.001f,
                                  "Frozen Texture must not advertise inaudible feedback");
        expect(presetProcessor.getProgramName(-1).isEmpty());
        expect(presetProcessor.getProgramName(presetProcessor.getNumPrograms()).isEmpty());

        beginTest("Internal bypass returns the latency-aligned dry signal");
        setParameter(processor, rl::params::bypass, 1.0f);
        juce::AudioBuffer<float> block(2, 256);
        juce::MidiBuffer midi;
        int absoluteSample = 0;
        int impulseAt = -1;
        for (int blockIndex = 0; blockIndex < 30; ++blockIndex)
        {
            block.clear();
            if (blockIndex == 0)
            {
                block.setSample(0, 0, 1.0f);
                block.setSample(1, 0, -1.0f);
            }
            processor.processBlock(block, midi);
            for (int sample = 0; sample < block.getNumSamples(); ++sample)
                if (impulseAt < 0 && std::abs(block.getSample(0, sample)) > 0.99f)
                    impulseAt = absoluteSample + sample;
            absoluteSample += block.getNumSamples();
        }
        expectEquals(impulseAt, 6600);

        beginTest("State restores free timing and editor dimensions");
        processor.setLastEditorSize(1234, 777);
        juce::MemoryBlock state;
        processor.getStateInformation(state);
        ReverseLabAudioProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        restored.prepareToPlay(48000.0, 64);
        expectWithinAbsoluteError(restored.parameters.getRawParameterValue(rl::params::rightFreeMs)->load(),
                                  137.5f, 0.01f);
        expectEquals(restored.getLastEditorSize().x, 1234);
        expectEquals(restored.getLastEditorSize().y, 777);
        expectEquals(restored.getLatencySamples(), 6600);

        beginTest("State restore rejects a valid XML tree belonging to another processor");
        {
            auto foreignState = state;
            auto* first = static_cast<char*>(foreignState.getData());
            auto* last = first + foreignState.getSize();
            constexpr char expectedType[] = "ReverseLabState";
            constexpr char foreignType[] = "InvalidLabState";
            static_assert(sizeof(expectedType) == sizeof(foreignType));
            auto* typePosition = std::search(first, last, std::begin(expectedType), std::end(expectedType) - 1);
            expect(typePosition != last, "Serialized state must contain its root type");
            if (typePosition != last)
                std::copy(std::begin(foreignType), std::end(foreignType) - 1, typePosition);

            ReverseLabAudioProcessor stateTarget;
            setParameter(stateTarget, rl::params::rightFreeMs, 321.0f);
            stateTarget.setLastEditorSize(1000, 700);
            stateTarget.setStateInformation(foreignState.getData(), static_cast<int>(foreignState.getSize()));
            expectWithinAbsoluteError(stateTarget.parameters.getRawParameterValue(rl::params::rightFreeMs)->load(),
                                      321.0f, 0.01f);
            expectEquals(stateTarget.getLastEditorSize().x, 1000);
            expectEquals(stateTarget.getLastEditorSize().y, 700);
        }

        beginTest("Variable block sizes and mono processing stay finite");
        ReverseLabAudioProcessor mono;
        juce::AudioProcessor::BusesLayout monoLayout;
        monoLayout.inputBuses.add(juce::AudioChannelSet::mono());
        monoLayout.outputBuses.add(juce::AudioChannelSet::mono());
        expect(mono.setBusesLayout(monoLayout));
        mono.prepareToPlay(96000.0, 2048);
        for (const auto blockSize : { 16, 31, 64, 255, 512, 2048 })
        {
            juce::AudioBuffer<float> monoBlock(1, blockSize);
            for (int i = 0; i < blockSize; ++i)
                monoBlock.setSample(0, i, std::sin(static_cast<float>(i) * 0.13f));
            mono.processBlock(monoBlock, midi);
            for (int i = 0; i < blockSize; ++i)
                expect(std::isfinite(monoBlock.getSample(0, i)));
        }


        beginTest("Bar timing follows the host time signature");
        ReverseLabAudioProcessor meterAware;
        setParameter(meterAware, rl::params::sync, 1.0f);
        setParameter(meterAware, rl::params::link, 1.0f);
        setParameter(meterAware, rl::params::leftSize, 13.0f);
        meterAware.prepareToPlay(48000.0, 64);
        TestPlayHead playHead;
        playHead.position.setBpm(120.0);
        playHead.position.setTimeSignature(juce::AudioPlayHead::TimeSignature { 3, 4 });
        playHead.position.setTimeInSamples(0);
        playHead.position.setPpqPosition(0.0);
        meterAware.setPlayHead(&playHead);
        juce::AudioBuffer<float> meterBlock(2, 64);
        meterBlock.clear();
        meterAware.processBlock(meterBlock, midi);
        meterAware.servicePendingHostUpdatesForTesting();
        meterAware.processBlock(meterBlock, midi);
        expectEquals(meterAware.getCurrentLatencySamples(), 72000);

        playHead.position.setTimeSignature(juce::AudioPlayHead::TimeSignature { 5, 4 });
        playHead.position.setTimeInSamples(64);
        for (int i = 0; i < 1200 && meterAware.getCurrentLatencySamples() != 120000; ++i)
        {
            meterBlock.clear();
            meterAware.processBlock(meterBlock, midi);
            if ((i & 31) == 0) meterAware.servicePendingHostUpdatesForTesting();
            playHead.position.setTimeInSamples(64LL * (i + 2));
        }
        expectEquals(meterAware.getCurrentLatencySamples(), 120000);
        expectWithinAbsoluteError(meterAware.getTailLengthSeconds(), 2.5, 0.01);
        meterAware.setPlayHead(nullptr);

        beginTest("Unlinked left and right times produce distinct stereo output");
        ReverseLabAudioProcessor stereoSplit;
        setParameter(stereoSplit, rl::params::sync, 0.0f);
        setParameter(stereoSplit, rl::params::link, 0.0f);
        setParameter(stereoSplit, rl::params::leftFreeMs, 50.0f);
        setParameter(stereoSplit, rl::params::rightFreeMs, 180.0f);
        setParameter(stereoSplit, rl::params::mix, 100.0f);
        setParameter(stereoSplit, rl::params::feedback, 0.0f);
        setParameter(stereoSplit, rl::params::stereoOffset, 0.0f);
        setParameter(stereoSplit, rl::params::random, 0.0f);
        stereoSplit.prepareToPlay(48000.0, 128);
        double differenceEnergy = 0.0;
        double leftEnergy = 0.0;
        double rightEnergy = 0.0;
        for (int blockIndex = 0; blockIndex < 240; ++blockIndex)
        {
            juce::AudioBuffer<float> stereoBlock(2, 128);
            for (int sample = 0; sample < 128; ++sample)
            {
                const auto absolute = blockIndex * 128 + sample;
                const auto value = absolute % 347 == 0 ? 0.8f
                    : 0.17f * std::sin(static_cast<float>(absolute) * 0.031f)
                      + 0.09f * std::sin(static_cast<float>(absolute) * 0.0073f);
                stereoBlock.setSample(0, sample, value);
                stereoBlock.setSample(1, sample, value);
            }
            stereoSplit.processBlock(stereoBlock, midi);
            if (blockIndex > 90)
                for (int sample = 0; sample < 128; ++sample)
                {
                    const auto leftSample = stereoBlock.getSample(0, sample);
                    const auto rightSample = stereoBlock.getSample(1, sample);
                    differenceEnergy += std::abs(leftSample - rightSample);
                    leftEnergy += std::abs(leftSample);
                    rightEnergy += std::abs(rightSample);
                }
        }
        expect(leftEnergy > 1.0 && rightEnergy > 1.0);
        expect(differenceEnergy > 100.0, "Unlinked L/R segment times must not collapse to mono");
        double scopeDifference = 0.0;
        for (int index = 0; index < 256; ++index)
            scopeDifference += std::abs(stereoSplit.getScopeSample(0, index)
                                        - stereoSplit.getScopeSample(1, index));
        expect(scopeDifference > 1.0, "The scope must display the actual independent L/R output");

        beginTest("Randomisation does not modulate host latency");
        ReverseLabAudioProcessor randomLatency;
        setParameter(randomLatency, rl::params::sync, 0.0f);
        setParameter(randomLatency, rl::params::link, 0.0f);
        setParameter(randomLatency, rl::params::leftFreeMs, 35.0f);
        setParameter(randomLatency, rl::params::rightFreeMs, 90.0f);
        setParameter(randomLatency, rl::params::random, 100.0f);
        randomLatency.prepareToPlay(48000.0, 64);
        const auto stableLatency = randomLatency.getLatencySamples();
        juce::AudioBuffer<float> randomBlock(2, 64);
        for (int blockIndex = 0; blockIndex < 400; ++blockIndex)
        {
            randomBlock.clear();
            randomLatency.processBlock(randomBlock, midi);
            if ((blockIndex & 15) == 0)
                randomLatency.servicePendingHostUpdatesForTesting();
            expectEquals(randomLatency.getLatencySamples(), stableLatency);
        }

        beginTest("Speed parameter automation is click-free through the full processor");
        {
            ReverseLabAudioProcessor automated;
            setParameter(automated, rl::params::sync, 0.0f);
            setParameter(automated, rl::params::link, 1.0f);
            setParameter(automated, rl::params::leftFreeMs, 500.0f);
            setParameter(automated, rl::params::mix, 100.0f);
            setParameter(automated, rl::params::feedback, 0.0f);
            setParameter(automated, rl::params::random, 0.0f);
            setParameter(automated, rl::params::stereoOffset, 0.0f);
            setParameter(automated, rl::params::speed, 1.0f);
            automated.prepareToPlay(48000.0, 256);
            constexpr float amplitude = 0.5f;
            constexpr float w = juce::MathConstants<float>::twoPi * 500.0f / 48000.0f;
            ClickDetector detector;
            juce::AudioBuffer<float> autoBlock(2, 256);
            int absolute = 0;
            for (int blockIndex = 0; blockIndex < 220; ++blockIndex)
            {
                if (blockIndex == 170) setParameter(automated, rl::params::speed, 2.0f); // ~20k samples into the 2nd segment
                for (int i = 0; i < 256; ++i)
                {
                    const auto value = amplitude * std::sin(w * static_cast<float>(absolute + i));
                    autoBlock.setSample(0, i, value);
                    autoBlock.setSample(1, i, value);
                }
                automated.processBlock(autoBlock, midi);
                for (int i = 0; i < 256; ++i)
                {
                    if (blockIndex >= 170) detector.push(autoBlock.getSample(0, i));
                    else detector.previous = autoBlock.getSample(0, i);
                }
                absolute += 256;
            }
            expectLessThan(detector.maxDelta, ClickDetector::threshold(amplitude, w, 2.0f),
                           "Smoothed speed automation must stay click-free end to end");
        }

        beginTest("Shortening the segment length is click-free across the wet alignment tap");
        {
            // A shorter segment makes the engine's active length drop at a segment boundary while
            // the host still reports the old, longer latency. The wet alignment offset therefore
            // jumps from 0 to (old - new) = 5280 samples; that change must be crossfaded, not
            // switched. Several non-commensurate test tones make sure a hard tap switch cannot
            // hide behind a whole-period offset or a lucky phase at the switch instant.
            for (const auto frequency : { 440.0f, 631.0f, 977.0f })
            {
                ReverseLabAudioProcessor shortening;
                setParameter(shortening, rl::params::sync, 0.0f);
                setParameter(shortening, rl::params::link, 1.0f);
                setParameter(shortening, rl::params::leftFreeMs, 300.0f);
                setParameter(shortening, rl::params::mix, 100.0f);
                setParameter(shortening, rl::params::feedback, 0.0f);
                setParameter(shortening, rl::params::random, 0.0f);
                setParameter(shortening, rl::params::stereoOffset, 0.0f);
                shortening.prepareToPlay(48000.0, 256);
                constexpr float amplitude = 0.5f;
                const float w = juce::MathConstants<float>::twoPi * frequency / 48000.0f;
                ClickDetector detector;
                juce::AudioBuffer<float> lengthBlock(2, 256);
                int absolute = 0;
                for (int blockIndex = 0; blockIndex < 400; ++blockIndex)
                {
                    if (blockIndex == 120) setParameter(shortening, rl::params::leftFreeMs, 190.0f);
                    for (int i = 0; i < 256; ++i)
                    {
                        const auto value = amplitude * std::sin(w * static_cast<float>(absolute + i));
                        lengthBlock.setSample(0, i, value);
                        lengthBlock.setSample(1, i, value);
                    }
                    shortening.processBlock(lengthBlock, midi);
                    if ((blockIndex & 15) == 0) shortening.servicePendingHostUpdatesForTesting();
                    for (int i = 0; i < 256; ++i)
                    {
                        if (blockIndex >= 120) detector.push(lengthBlock.getSample(0, i));
                        else detector.previous = lengthBlock.getSample(0, i);
                    }
                    absolute += 256;
                }
                expectEquals(shortening.getCurrentLatencySamples(), 9120);
                logMessage("  " + juce::String(frequency, 0) + " Hz: max step " + juce::String(detector.maxDelta, 4)
                           + " (limit " + juce::String(ClickDetector::threshold(amplitude, w, 1.0f), 4) + ")");
                expectLessThan(detector.maxDelta, ClickDetector::threshold(amplitude, w, 1.0f),
                               "Segment shortening must crossfade the wet alignment tap");
            }
        }

        beginTest("processBlock before prepareToPlay passes audio through unharmed");
        {
            ReverseLabAudioProcessor unprepared2;
            juce::AudioBuffer<float> raw(2, 64);
            for (int i = 0; i < 64; ++i) { raw.setSample(0, i, 0.25f); raw.setSample(1, i, -0.25f); }
            unprepared2.processBlock(raw, midi);
            expectWithinAbsoluteError(raw.getSample(0, 10), 0.25f, 0.0001f);
            expectWithinAbsoluteError(raw.getSample(1, 10), -0.25f, 0.0001f);
        }

        beginTest("releaseResources frees history and permits a clean re-prepare");
        {
            ReverseLabAudioProcessor releasable;
            releasable.prepareToPlay(192000.0, 2048);
            expect(releasable.getAllocatedHistoryBytesForTesting() > 80u * 1024u * 1024u);
            releasable.releaseResources();
            expect(releasable.getAllocatedHistoryBytesForTesting() == 0u);
            expectEquals(releasable.getScopeWriteIndex(), 0);
            expectWithinAbsoluteError(releasable.getScopeSample(0, 0), 0.0f, 0.0001f);
            expectWithinAbsoluteError(releasable.getScopeSample(1, 0), 0.0f, 0.0001f);

            juce::AudioBuffer<float> releasedBlock(2, 64);
            for (int i = 0; i < 64; ++i)
            {
                releasedBlock.setSample(0, i, 0.25f);
                releasedBlock.setSample(1, i, -0.25f);
            }
            releasable.processBlock(releasedBlock, midi);
            expectWithinAbsoluteError(releasedBlock.getSample(0, 10), 0.25f, 0.0001f);
            expectWithinAbsoluteError(releasedBlock.getSample(1, 10), -0.25f, 0.0001f);
            releasable.prepareToPlay(48000.0, 64);
            expect(releasable.getAllocatedHistoryBytesForTesting() > 20u * 1024u * 1024u);
        }

        beginTest("prepareToPlay reports latency from the host tempo when available");
        {
            ReverseLabAudioProcessor tempoAware;
            setParameter(tempoAware, rl::params::sync, 1.0f);
            setParameter(tempoAware, rl::params::link, 1.0f);
            setParameter(tempoAware, rl::params::leftSize, 8.0f); // 1/4
            TestPlayHead hostAt90;
            hostAt90.position.setBpm(90.0);
            tempoAware.setPlayHead(&hostAt90);
            tempoAware.prepareToPlay(48000.0, 64);
            expectEquals(tempoAware.getLatencySamples(), 32000); // 60 / 90 * 48000
            tempoAware.setPlayHead(nullptr);
        }

        beginTest("Reported tail follows feedback decay");
        ReverseLabAudioProcessor tail;
        setParameter(tail, rl::params::sync, 0.0f);
        setParameter(tail, rl::params::leftFreeMs, 4000.0f);
        setParameter(tail, rl::params::feedback, 95.0f);
        tail.prepareToPlay(48000.0, 64);
        expect(tail.getTailLengthSeconds() > 500.0);
    }
};

static ReverseEngineTests reverseEngineTests;
static ProcessorTests processorTests;

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    juce::UnitTestRunner runner;
    runner.runAllTests();
    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        if (auto* result = runner.getResult(i)) failures += result->failures;
    return failures == 0 ? 0 : 1;
}
