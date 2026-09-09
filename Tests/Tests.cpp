#include <juce_core/juce_core.h>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ReverseEngine.h"
#include "FactoryBank.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <thread>
#include <vector>

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

struct EchoingProgramHost final : juce::AudioProcessorListener
{
    explicit EchoingProgramHost(ReverseLabAudioProcessor& processorIn) : processor(processorIn) {}

    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override
    {
        ++parameterNotifications;
    }

    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override
    {
        if (! details.programChanged)
            return;
        ++programNotifications;
        processor.setCurrentProgram(processor.getCurrentProgram());
    }

    ReverseLabAudioProcessor& processor;
    int programNotifications = 0;
    int parameterNotifications = 0;
};

struct CascadingProgramHost final : juce::AudioProcessorListener
{
    explicit CascadingProgramHost(ReverseLabAudioProcessor& processorIn) : processor(processorIn) {}

    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}

    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override
    {
        if (! details.programChanged)
            return;
        programs.push_back(processor.getCurrentProgram());
        if (programs.size() == 1)
            processor.setCurrentProgram(2);
    }

    ReverseLabAudioProcessor& processor;
    std::vector<int> programs;
};

// A processor listener models the host callback reached by setValueNotifyingHost,
// not the later programChanged notification that the older cascade tests cover.
struct ParameterCallbackHost final : juce::AudioProcessorListener
{
    ParameterCallbackHost(ReverseLabAudioProcessor& p, std::function<void(int)> callbackIn)
        : processor(p), callback(std::move(callbackIn)) { processor.addListener(this); }
    ~ParameterCallbackHost() override { processor.removeListener(this); }
    void audioProcessorParameterChanged(juce::AudioProcessor*, int index, float) override
    {
        ++parameterNotifications;
        ++depth;
        maximumDepth = juce::jmax(maximumDepth, depth);
        callback(index);
        --depth;
    }
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override
    { if (details.programChanged) ++programNotifications; }
    ReverseLabAudioProcessor& processor;
    std::function<void(int)> callback;
    int parameterNotifications = 0, programNotifications = 0, depth = 0, maximumDepth = 0;
};

juce::MemoryBlock snapshot(ReverseLabAudioProcessor& processor)
{
    juce::MemoryBlock result;
    processor.getStateInformation(result);
    return result;
}

void restore(ReverseLabAudioProcessor& processor, const juce::MemoryBlock& state)
{
    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
}

juce::ValueTree stateTree(const juce::MemoryBlock& state)
{
    if (auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize())))
        return juce::ValueTree::fromXml(*xml);
    return {};
}

bool sameCompleteState(ReverseLabAudioProcessor& schema, const juce::MemoryBlock& actual,
                       const juce::MemoryBlock& expected)
{
    const auto a = stateTree(actual), b = stateTree(expected);
    if (! a.hasType("ReverseLabState") || ! b.hasType("ReverseLabState")
        || a.getNumChildren() != b.getNumChildren()) return false;
    for (const auto* property : { "program", "editorWidth", "editorHeight" })
        if (! a.hasProperty(property) || ! b.hasProperty(property) || a[property] != b[property]) return false;
    if (! a.getChildWithName("WkPresetSelection").isEquivalentTo(b.getChildWithName("WkPresetSelection")))
        return false;
    for (const auto& [id, ignored] : factoryBank().front().values)
    {
        juce::ignoreUnused(ignored);
        const auto left = a.getChildWithProperty("id", id), right = b.getChildWithProperty("id", id);
        if (! left.hasProperty("value") || ! right.hasProperty("value")) return false;
        const auto x = static_cast<float>(left["value"]), y = static_cast<float>(right["value"]);
        const auto& range = schema.parameters.getParameter(id)->getNormalisableRange();
        if (! std::isfinite(x) || ! std::isfinite(y) || x < range.start || x > range.end
            || y < range.start || y > range.end) return false;
        // APVTS can retain a neighbouring floating representation of the same legal
        // step. Compare exact parameter-grid values, never a widened float tolerance.
        if (std::abs(range.snapToLegalValue(x) - range.snapToLegalValue(y)) > 0.0f) return false;
    }
    return true;
}

bool liveStateMatches(ReverseLabAudioProcessor& processor, const juce::MemoryBlock& expected, bool requireRaw = true)
{
    const auto tree = stateTree(expected);
    if (! tree.hasType("ReverseLabState") || ! tree.hasProperty("program")
        || ! tree.hasProperty("editorWidth") || ! tree.hasProperty("editorHeight")) return false;
    const auto program = static_cast<int>(tree["program"]);
    if (! juce::isPositiveAndBelow(program, processor.getNumPrograms()) || processor.getCurrentProgram() != program)
        return false;
    const auto size = processor.getLastEditorSize();
    if (size.x != static_cast<int>(tree["editorWidth"]) || size.y != static_cast<int>(tree["editorHeight"])) return false;
    for (const auto& [id, ignored] : factoryBank().front().values)
    {
        juce::ignoreUnused(ignored);
        const auto child = tree.getChildWithProperty("id", id);
        if (! child.hasProperty("value")) return false;
        const auto* parameter = processor.parameters.getParameter(id);
        const auto& range = parameter->getNormalisableRange();
        const auto expectedValue = static_cast<float>(child["value"]);
        const auto parameterValue = parameter->convertFrom0to1(parameter->getValue());
        // Ranged values commit synchronously; APVTS raw is a notification-driven
        // cache checked separately after the drain, never a relaxed comparison.
        const auto raw = requireRaw ? processor.parameters.getRawParameterValue(id)->load() : parameterValue;
        for (const auto value : { raw, parameterValue, expectedValue })
            if (! std::isfinite(value) || value < range.start || value > range.end) return false;
        if (std::abs(range.snapToLegalValue(raw) - range.snapToLegalValue(expectedValue)) > 0.0f
            || std::abs(range.snapToLegalValue(parameterValue) - range.snapToLegalValue(expectedValue)) > 0.0f) return false;
    }
    const auto selected = tree.getChildWithName("WkPresetSelection");
    const auto current = processor.presets.current();
    if (! selected.isValid())
        return current.id == factoryBank()[static_cast<size_t>(program)].id;
    if (current.id != selected["id"].toString() || current.name != selected["name"].toString()
        || current.category != selected["category"].toString() || current.description != selected["description"].toString()
        || current.values.size() != factoryBank().front().values.size()) return false;
    for (const auto& [id, value] : current.values)
    {
        const auto child = selected.getChildWithProperty("id", id);
        if (! child.hasProperty("value") || ! std::isfinite(value)) return false;
        const auto& range = processor.parameters.getParameter(id)->getNormalisableRange();
        if (std::abs(range.snapToLegalValue(value) - range.snapToLegalValue(static_cast<float>(child["value"]))) > 0.0f)
            return false;
    }
    return true;
}

void makeCustomState(ReverseLabAudioProcessor& processor, int program, bool alternate)
{
    processor.setCurrentProgram(program);
    setParameter(processor, rl::params::bypass, alternate ? 0.0f : 1.0f);
    setParameter(processor, rl::params::crossfade, alternate ? 7.0f : 11.0f);
    setParameter(processor, rl::params::feedback, alternate ? 29.0f : 13.0f);
    setParameter(processor, rl::params::mix, alternate ? 53.0f : 37.0f);
    setParameter(processor, rl::params::output, alternate ? -7.0f : -11.0f);
    processor.setLastEditorSize(alternate ? 1180 : 1040, alternate ? 780 : 680);
    juce::ValueTree selection("WkPresetSelection"), state("ReverseLabState");
    selection.setProperty("id", alternate ? "00000000000000000000000000000002"
                                           : "00000000000000000000000000000001", nullptr);
    selection.setProperty("name", alternate ? "Callback Z" : "Callback Y", nullptr);
    selection.setProperty("category", "Regression", nullptr);
    for (const auto& [id, ignored] : factoryBank().front().values)
    {
        juce::ignoreUnused(ignored);
        juce::ValueTree parameter("VALUE");
        parameter.setProperty("id", id, nullptr);
        parameter.setProperty("value", processor.parameters.getRawParameterValue(id)->load(), nullptr);
        selection.addChild(parameter, -1, nullptr);
    }
    state.addChild(selection, -1, nullptr);
    processor.presets.restoreSelection(state); // Metadata fixture only; never writes a preset file.
}

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

        beginTest("Fractional speed keeps precision at long read offsets");
        {
            rl::ReverseEngine precisionEngine;
            precisionEngine.prepare(48000.0, 48000);
            rl::EngineSettings precisionSettings;
            precisionSettings.leftLength = precisionSettings.rightLength = 64;
            precisionSettings.speed = 1.37f;
            (void) precisionEngine.processSample(0, 0.0f, precisionSettings);
            (void) precisionEngine.processSample(1, 0.0f, precisionSettings);
            precisionEngine.advance();

            constexpr double longOffset = 2097152.0; // float ULP is 0.25 samples here
            precisionEngine.setReadOffsetForTesting(0, longOffset);
            (void) precisionEngine.processSample(0, 0.0f, precisionSettings);
            const auto advanced = precisionEngine.getReadOffsetForTesting(0) - longOffset;
            expect(std::abs(advanced - static_cast<double>(precisionSettings.speed)) < 1.0e-9,
                   "The accumulated read offset must advance by the requested fractional speed");
        }

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
        float energy = 0.0f, lastSegmentEnergy = 0.0f;
        for (int i = 0; i < 128 * 8; ++i) // eight segment lengths: the texture must not fade out
        {
            const auto wet = std::abs(engine.processSample(0, 0.0f, settings));
            (void) engine.processSample(1, 0.0f, settings);
            engine.advance();
            energy += wet;
            if (i >= 128 * 7) lastSegmentEnergy += wet;
        }
        expect(energy > 1.0f);
        expect(lastSegmentEnergy > 1.0f, "A frozen capture must still play after several segment lengths");

        beginTest("Freeze requested before capture pre-rolls instead of latching silence");
        {
            rl::ReverseEngine frozenFromStart;
            frozenFromStart.prepare(48000.0, 4096);
            rl::EngineSettings s;
            s.leftLength = s.rightLength = 64;
            s.freeze = true;
            float frozenFromStartEnergy = 0.0f;
            for (int i = 0; i < 256; ++i)
            {
                const auto input = 0.4f * std::sin(static_cast<float>(i) * 0.17f);
                const auto frozenOutput = frozenFromStart.processSample(0, input, s);
                (void) frozenFromStart.processSample(1, input, s);
                if (i >= 96) frozenFromStartEnergy += std::abs(frozenOutput);
                frozenFromStart.advance();
            }
            expect(frozenFromStartEnergy > 1.0f,
                   "A fresh frozen engine must capture one complete segment before holding it");
        }

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

        beginTest("Non-finite and out-of-range normalized host writes cannot poison tail or engine conversion");
        {
            struct InvalidNormalised { const char* name; float value; };
            const std::array invalidValues {
                InvalidNormalised { "NaN", std::numeric_limits<float>::quiet_NaN() },
                InvalidNormalised { "+Inf", std::numeric_limits<float>::infinity() },
                InvalidNormalised { "-Inf", -std::numeric_limits<float>::infinity() },
                InvalidNormalised { "below zero", -0.01f },
                InvalidNormalised { "above one", 1.01f }
            };
            const auto configure = [](ReverseLabAudioProcessor& subject)
            {
                setParameter(subject, rl::params::sync, 0.0f);
                setParameter(subject, rl::params::link, 1.0f);
                setParameter(subject, rl::params::leftFreeMs, 20.0f);
                setParameter(subject, rl::params::rightFreeMs, 20.0f);
                setParameter(subject, rl::params::speed, 1.0f);
                setParameter(subject, rl::params::mix, 100.0f);
                setParameter(subject, rl::params::feedback, 0.0f);
                setParameter(subject, rl::params::random, 0.0f);
                setParameter(subject, rl::params::stereoOffset, 0.0f);
                setParameter(subject, rl::params::bypass, 0.0f);
            };
            for (const auto& invalid : invalidValues)
            {
                ReverseLabAudioProcessor subject, reference;
                configure(subject);
                configure(reference);
                subject.prepareToPlay(48000.0, 64);
                reference.prepareToPlay(48000.0, 64);

                int position = 0;
                float maximumDifference = 0.0f;
                bool finite = true;
                double comparedEnergy = 0.0;
                const auto renderPair = [&](int blocks, bool compare)
                {
                    juce::MidiBuffer midi;
                    for (int blockIndex = 0; blockIndex < blocks; ++blockIndex)
                    {
                        juce::AudioBuffer<float> actual(2, 64), expected(2, 64);
                        for (int sample = 0; sample < 64; ++sample)
                            for (int channel = 0; channel < 2; ++channel)
                            {
                                const auto input = 0.2f * std::sin(static_cast<float>(position + sample)
                                                                   * (channel == 0 ? 0.071f : 0.113f));
                                actual.setSample(channel, sample, input);
                                expected.setSample(channel, sample, input);
                            }
                        subject.processBlock(actual, midi);
                        reference.processBlock(expected, midi);
                        if (compare)
                            for (int sample = 0; sample < 64; ++sample)
                                for (int channel = 0; channel < 2; ++channel)
                                {
                                    const auto a = actual.getSample(channel, sample);
                                    const auto b = expected.getSample(channel, sample);
                                    finite = finite && std::isfinite(a) && std::isfinite(b);
                                    maximumDifference = juce::jmax(maximumDifference, std::abs(a - b));
                                    comparedEnergy += static_cast<double>(b) * static_cast<double>(b);
                                }
                        position += 64;
                    }
                };

                renderPair(48, false); // Ensure the next speed read reaches active reverse playback.
                auto* speed = subject.parameters.getParameter(rl::params::speed);
                speed->setValue(invalid.value); // Models a malformed normalised host-automation write.
                const auto stored = speed->getValue();
                const auto storedIsInvalid = ! std::isfinite(stored) || stored < 0.0f || stored > 1.0f;
                if (std::isnan(invalid.value))
                    expect(storedIsInvalid, "NaN must exercise readParameter's fallback instead of a JUCE endpoint clamp");
                else
                    expect(! storedIsInvalid,
                           juce::String(invalid.name) + " is expected to be clipped by the standard JUCE float parameter");
                reference.parameters.getParameter(rl::params::speed)->setValue(
                    storedIsInvalid ? speed->getDefaultValue() : stored);
                const auto subjectTail = subject.getTailLengthSeconds();
                const auto referenceTail = reference.getTailLengthSeconds();
                expect(std::isfinite(subjectTail) && subjectTail > 0.0,
                       juce::String(invalid.name) + " must not poison the host tail query");
                expectWithinAbsoluteError(subjectTail, referenceTail, 0.0,
                                          juce::String(invalid.name) + " must use the default or JUCE's legal clipped endpoint");
                renderPair(8, true);
                expect(finite, juce::String(invalid.name) + " must not reach the engine as a non-finite position");
                expectGreaterThan(comparedEnergy, 0.01, juce::String(invalid.name) + " must exercise audible reverse DSP");
                expectWithinAbsoluteError(maximumDifference, 0.0f, 0.0f,
                                          juce::String(invalid.name) + " must render exactly like the default-speed reference");
            }
        }

        beginTest("Legal normalized host automation remains effective");
        {
            ReverseLabAudioProcessor legal, reference;
            for (auto* subject : { &legal, &reference })
            {
                setParameter(*subject, rl::params::sync, 0.0f);
                setParameter(*subject, rl::params::link, 1.0f);
                setParameter(*subject, rl::params::leftFreeMs, 20.0f);
                setParameter(*subject, rl::params::mix, 100.0f);
                setParameter(*subject, rl::params::feedback, 0.0f);
            }
            legal.parameters.getParameter(rl::params::speed)->setValue(1.0f); // legal normalised maximum = 4x
            const auto saved = stateTree(snapshot(legal));
            expectWithinAbsoluteError(static_cast<float>(saved.getChildWithProperty("id", rl::params::speed)["value"]),
                                      4.0f, 0.0f, "A legal host value must not fall back to the 1x default");
            legal.prepareToPlay(48000.0, 64);
            reference.prepareToPlay(48000.0, 64);
            expect(legal.getTailLengthSeconds() > reference.getTailLengthSeconds(),
                   "The safe tail path must retain valid 4x speed automation");
        }

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
        EchoingProgramHost programHost(presetProcessor);
        presetProcessor.addListener(&programHost);
        presetProcessor.setCurrentProgram(3);
        const auto parameterNotificationsAfterChange = programHost.parameterNotifications;
        expectGreaterThan(parameterNotificationsAfterChange, 0,
                          "The first factory-program application must exercise parameter notifications");
        presetProcessor.servicePendingHostUpdatesForTesting();
        expectEquals(presetProcessor.getCurrentProgram(), 3);
        expectEquals(programHost.programNotifications, 1,
                     "A changed factory program must be reported exactly once even when the host echoes it");
        expectEquals(programHost.parameterNotifications, parameterNotificationsAfterChange,
                     "A host echo must not apply the same factory program a second time");
        expectWithinAbsoluteError(presetProcessor.parameters.getRawParameterValue(rl::params::feedback)->load(),
                                  72.0f, 0.001f);
        expectWithinAbsoluteError(presetProcessor.parameters.getRawParameterValue(rl::params::bypass)->load(),
                                  0.0f, 0.001f);
        presetProcessor.setCurrentProgram(2);
        presetProcessor.servicePendingHostUpdatesForTesting();
        expectEquals(programHost.programNotifications, 2);
        expectWithinAbsoluteError(presetProcessor.parameters.getRawParameterValue(rl::params::freeze)->load(),
                                  1.0f, 0.001f);
        expectWithinAbsoluteError(presetProcessor.parameters.getRawParameterValue(rl::params::feedback)->load(),
                                  0.0f, 0.001f,
                                  "Frozen Texture must not advertise inaudible feedback");
        expect(presetProcessor.getProgramName(-1).isEmpty());
        expect(presetProcessor.getProgramName(presetProcessor.getNumPrograms()).isEmpty());
        presetProcessor.removeListener(&programHost);

        beginTest("A different reentrant host program request is applied once on the next service tick");
        ReverseLabAudioProcessor cascadingProcessor;
        CascadingProgramHost cascadingHost(cascadingProcessor);
        cascadingProcessor.addListener(&cascadingHost);
        cascadingProcessor.setCurrentProgram(3);
        expectEquals(cascadingProcessor.getCurrentProgram(), 3);
        cascadingProcessor.servicePendingHostUpdatesForTesting();
        cascadingProcessor.servicePendingHostUpdatesForTesting();
        expectEquals(cascadingProcessor.getCurrentProgram(), 2);
        expectEquals(static_cast<int>(cascadingHost.programs.size()), 2);
        if (cascadingHost.programs.size() == 2)
        {
            expectEquals(cascadingHost.programs[0], 3);
            expectEquals(cascadingHost.programs[1], 2);
        }
        cascadingProcessor.removeListener(&cascadingHost);

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

        beginTest("Editor size snapshots stay coherent across threads");
        ReverseLabAudioProcessor coherentSize;
        coherentSize.setLastEditorSize(720, 460);
        std::atomic<bool> startSizeWriters { false };
        std::atomic<bool> stopSizeWriters { false };
        std::atomic<int> writersWithFirstStore { 0 };
        const auto writeSize = [&](int width, int height)
        {
            while (! startSizeWriters.load(std::memory_order_acquire))
                std::this_thread::yield();
            coherentSize.setLastEditorSize(width, height);
            writersWithFirstStore.fetch_add(1, std::memory_order_release);
            while (! stopSizeWriters.load(std::memory_order_acquire))
                coherentSize.setLastEditorSize(width, height);
        };
        std::thread smallSizeWriter(writeSize, 720, 460);
        std::thread largeSizeWriter(writeSize, 1440, 920);
        startSizeWriters.store(true, std::memory_order_release);
        while (writersWithFirstStore.load(std::memory_order_acquire) != 2)
            std::this_thread::yield();
        for (int read = 0; read < 100000; ++read)
        {
            const auto size = coherentSize.getLastEditorSize();
            expect((size.x == 720 && size.y == 460) || (size.x == 1440 && size.y == 920),
                   "Width and height must come from the same published snapshot");
        }
        stopSizeWriters.store(true, std::memory_order_release);
        smallSizeWriter.join();
        largeSizeWriter.join();

        beginTest("An open editor immediately adopts restored dimensions");
        ReverseLabAudioProcessor visibleRestore;
        if (auto* editor = visibleRestore.createEditorAndMakeActive())
        {
            visibleRestore.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            expectEquals(editor->getWidth(), 1234,
                         "An open editor must immediately adopt the restored width");
            expectEquals(editor->getHeight(), 777,
                         "An open editor must immediately adopt the restored height");
            visibleRestore.editorBeingDeleted(editor);
            delete editor;
        }
        else
        {
            expect(false, "The processor must create its editor for the state-restore test");
        }

        beginTest("An open editor adopts dimensions restored from a worker thread on its timer");
        ReverseLabAudioProcessor backgroundRestore;
        if (auto* editor = dynamic_cast<ReverseLabAudioProcessorEditor*>(
                backgroundRestore.createEditorAndMakeActive()))
        {
            std::thread restoreWorker([&]
            {
                backgroundRestore.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            });
            restoreWorker.join();
            editor->serviceTimerForTesting();
            expectEquals(editor->getWidth(), 1234);
            expectEquals(editor->getHeight(), 777);
            backgroundRestore.editorBeingDeleted(editor);
            delete editor;
        }
        else
        {
            expect(false, "The processor must create its editor for the background-restore test");
        }

        beginTest("A pending worker-thread size restore survives closing and reopening the editor");
        ReverseLabAudioProcessor closeBeforeRestoreTick;
        if (auto* editor = closeBeforeRestoreTick.createEditorAndMakeActive())
        {
            std::thread restoreWorker([&]
            {
                closeBeforeRestoreTick.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
            });
            restoreWorker.join();
            closeBeforeRestoreTick.editorBeingDeleted(editor);
            delete editor;

            if (auto* reopenedEditor = closeBeforeRestoreTick.createEditorAndMakeActive())
            {
                expectEquals(reopenedEditor->getWidth(), 1234);
                expectEquals(reopenedEditor->getHeight(), 777);
                closeBeforeRestoreTick.editorBeingDeleted(reopenedEditor);
                delete reopenedEditor;
            }
            else
            {
                expect(false, "The processor must reopen its editor after a background restore");
            }
        }
        else
        {
            expect(false, "The processor must create its editor for the close-before-tick test");
        }

        beginTest("A restored Freeze state captures fresh material instead of remaining silent");
        {
            ReverseLabAudioProcessor frozenSource;
            setParameter(frozenSource, rl::params::sync, 0.0f);
            setParameter(frozenSource, rl::params::link, 1.0f);
            setParameter(frozenSource, rl::params::leftFreeMs, 20.0f);
            setParameter(frozenSource, rl::params::mix, 100.0f);
            setParameter(frozenSource, rl::params::freeze, 1.0f);
            juce::MemoryBlock frozenState;
            frozenSource.getStateInformation(frozenState);

            ReverseLabAudioProcessor frozenRestored;
            frozenRestored.setStateInformation(frozenState.getData(), static_cast<int>(frozenState.getSize()));
            frozenRestored.prepareToPlay(48000.0, 64);
            juce::AudioBuffer<float> frozenBlock(2, 64);
            juce::MidiBuffer frozenMidi;
            double frozenEnergy = 0.0;
            int absolute = 0;
            for (int blockIndex = 0; blockIndex < 80; ++blockIndex)
            {
                for (int sample = 0; sample < frozenBlock.getNumSamples(); ++sample)
                {
                    const auto input = 0.4f * std::sin(static_cast<float>(absolute + sample) * 0.071f);
                    frozenBlock.setSample(0, sample, input);
                    frozenBlock.setSample(1, sample, input);
                }
                frozenRestored.processBlock(frozenBlock, frozenMidi);
                if (blockIndex >= 32)
                    for (int sample = 0; sample < frozenBlock.getNumSamples(); ++sample)
                        frozenEnergy += std::abs(frozenBlock.getSample(0, sample));
                absolute += frozenBlock.getNumSamples();
            }
            expect(frozenEnergy > 10.0,
                   "Restoring Freeze without serialized audio must not latch a 100%-wet instance at zero");
        }

        beginTest("A frozen capture survives transport restart and loop jumps");
        {
            // Two identical instances: both freeze the same 200 ms sine capture. One then sees a
            // transport loop jump (position back to zero) followed by a stop/start while the input
            // is silent. Its output must keep playing the frozen texture exactly like the reference.
            auto makeFrozen = [&](ReverseLabAudioProcessor& p, TestPlayHead& head)
            {
                setParameter(p, rl::params::sync, 0.0f);
                setParameter(p, rl::params::link, 1.0f);
                setParameter(p, rl::params::leftFreeMs, 200.0f);
                setParameter(p, rl::params::mix, 100.0f);
                setParameter(p, rl::params::feedback, 0.0f);
                setParameter(p, rl::params::random, 0.0f);
                setParameter(p, rl::params::stereoOffset, 0.0f);
                setParameter(p, rl::params::freeze, 1.0f);
                head.position.setIsPlaying(true);
                head.position.setTimeInSamples(0);
                p.setPlayHead(&head);
                p.prepareToPlay(48000.0, 256);
            };
            ReverseLabAudioProcessor reference, looped;
            TestPlayHead referenceHead, loopedHead;
            makeFrozen(reference, referenceHead);
            makeFrozen(looped, loopedHead);
            juce::AudioBuffer<float> referenceBlock(2, 256), loopedBlock(2, 256);
            constexpr float w = juce::MathConstants<float>::twoPi * 440.0f / 48000.0f;
            int64_t referenceTime = 0, loopedTime = 0;
            float maxDifference = 0.0f, energyAfterJump = 0.0f, referenceEnergyAfterJump = 0.0f, energyBeforeJump = 0.0f;
            for (int blockIndex = 0; blockIndex < 400; ++blockIndex)
            {
                const bool capturing = blockIndex < 150; // ~0.8 s: pre-roll completes, Freeze engages
                for (int i = 0; i < 256; ++i)
                {
                    const auto value = capturing ? 0.5f * std::sin(w * static_cast<float>(blockIndex * 256 + i)) : 0.0f;
                    referenceBlock.setSample(0, i, value); referenceBlock.setSample(1, i, value);
                    loopedBlock.setSample(0, i, value);    loopedBlock.setSample(1, i, value);
                }
                if (blockIndex == 200) loopedTime = 0;                       // loop cycle: jump back to the start
                if (blockIndex == 300) loopedHead.position.setIsPlaying(false); // stop ...
                if (blockIndex == 302) loopedHead.position.setIsPlaying(true);  // ... and start again
                referenceHead.position.setTimeInSamples(referenceTime);
                loopedHead.position.setTimeInSamples(loopedTime);
                reference.processBlock(referenceBlock, midi);
                looped.processBlock(loopedBlock, midi);
                referenceTime += 256; loopedTime += 256;
                if (blockIndex >= 200)
                    for (int i = 0; i < 256; ++i)
                    {
                        maxDifference = juce::jmax(maxDifference, std::abs(referenceBlock.getSample(0, i) - loopedBlock.getSample(0, i)));
                        energyAfterJump += loopedBlock.getSample(0, i) * loopedBlock.getSample(0, i);
                        referenceEnergyAfterJump += referenceBlock.getSample(0, i) * referenceBlock.getSample(0, i);
                    }
                else if (blockIndex >= 150)
                    for (int i = 0; i < 256; ++i) energyBeforeJump += loopedBlock.getSample(0, i) * loopedBlock.getSample(0, i);
            }
            logMessage("  energy 150-199: " + juce::String(energyBeforeJump) + "  looped after: " + juce::String(energyAfterJump) + "  reference after: " + juce::String(referenceEnergyAfterJump));
            expectGreaterThan(energyAfterJump, 1.0f, "The frozen texture must keep sounding after the jump");
            expectLessThan(maxDifference, 1.0e-3f, "Loop jumps and stop/start must not replace a frozen capture");
            reference.setPlayHead(nullptr);
            looped.setPlayHead(nullptr);
        }

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

        beginTest("Extreme and non-finite host positions cannot overflow transport handling");
        {
            ReverseLabAudioProcessor hostileTransport;
            TestPlayHead hostilePlayHead;
            hostilePlayHead.position.setIsPlaying(true);
            hostilePlayHead.position.setBpm(120.0);
            hostilePlayHead.position.setPpqPosition(std::numeric_limits<double>::quiet_NaN());
            hostilePlayHead.position.setTimeInSamples(std::numeric_limits<int64_t>::max());
            hostileTransport.setPlayHead(&hostilePlayHead);
            hostileTransport.prepareToPlay(48000.0, 64);
            juce::AudioBuffer<float> hostileBlock(2, 64);
            hostileBlock.clear();
            hostileTransport.processBlock(hostileBlock, midi);
            hostilePlayHead.position.setTimeInSamples(std::numeric_limits<int64_t>::min());
            hostileTransport.processBlock(hostileBlock, midi);
            for (int sample = 0; sample < hostileBlock.getNumSamples(); ++sample)
                expect(std::isfinite(hostileBlock.getSample(0, sample)));
            hostileTransport.setPlayHead(nullptr);
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
        // Tail includes reverse read age and output alignment, not just one bar's duration.
        expectWithinAbsoluteError(meterAware.getTailLengthSeconds(), 7.6, 0.01);
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

        beginTest("Filter Off automation crossfades instead of switching topology");
        {
            ReverseLabAudioProcessor filterAutomation;
            setParameter(filterAutomation, rl::params::sync, 0.0f);
            setParameter(filterAutomation, rl::params::link, 1.0f);
            setParameter(filterAutomation, rl::params::leftFreeMs, 20.0f);
            setParameter(filterAutomation, rl::params::mix, 100.0f);
            setParameter(filterAutomation, rl::params::feedback, 0.0f);
            setParameter(filterAutomation, rl::params::crossfade, 25.0f);
            setParameter(filterAutomation, rl::params::highpass, 1000.0f);
            setParameter(filterAutomation, rl::params::lowpass, 20000.0f);
            filterAutomation.prepareToPlay(48000.0, 64);
            juce::AudioBuffer<float> filterBlock(2, 64);
            juce::MidiBuffer filterMidi;
            for (int blockIndex = 0; blockIndex < 100; ++blockIndex)
            {
                for (int sample = 0; sample < filterBlock.getNumSamples(); ++sample)
                    for (int channel = 0; channel < 2; ++channel)
                        filterBlock.setSample(channel, sample, 0.5f);
                filterAutomation.processBlock(filterBlock, filterMidi);
            }

            ClickDetector filterDetector;
            filterDetector.previous = filterBlock.getSample(0, filterBlock.getNumSamples() - 1);
            setParameter(filterAutomation, rl::params::highpass, 20.0f);
            for (int blockIndex = 0; blockIndex < 48; ++blockIndex)
            {
                for (int sample = 0; sample < filterBlock.getNumSamples(); ++sample)
                    for (int channel = 0; channel < 2; ++channel)
                        filterBlock.setSample(channel, sample, 0.5f);
                filterAutomation.processBlock(filterBlock, filterMidi);
                for (int sample = 0; sample < filterBlock.getNumSamples(); ++sample)
                    filterDetector.push(filterBlock.getSample(0, sample));
            }
            expectLessThan(filterDetector.maxDelta, 0.02f,
                           "Moving High-pass to Off must not jump from filtered to dry in one sample");
            expect(filterBlock.getSample(0, filterBlock.getNumSamples() - 1) > 0.45f,
                   "High-pass Off must finish at the unfiltered signal");
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

class AcceptanceRegressionTests final : public juce::UnitTest
{
public:
    AcceptanceRegressionTests() : UnitTest("Acceptance regressions", "Integration") {}

    void runTest() override
    {
        beginTest("Feedback decays across crossfade, speed, length and history-offset extremes");
        {
            int cases = 0;
            float worstTailPeak = 0.0f;
            for (const auto length : { 64, 960 })
                for (const auto speed : { 0.25f, 1.0f, 4.0f })
                    for (const auto crossfade : { 0.0f, 0.04f, 0.25f })
                        for (const auto feedback : { 0.72f, 0.95f })
                            for (const auto modulated : { false, true })
                            {
                                rl::ReverseEngine engine;
                                const auto capacity = juce::jmax(4096, length * 8 + 8);
                                engine.prepare(48000.0, length * 8);
                                rl::EngineSettings settings;
                                settings.leftLength = length;
                                settings.rightLength = juce::jmax(16, length / 2);
                                settings.speed = speed;
                                settings.crossfade = crossfade;
                                settings.feedback = feedback;
                                settings.randomAmount = modulated ? 1.0f : 0.0f;
                                settings.stereoOffset = modulated ? 1.0f : 0.0f;
                                const auto repeats = static_cast<int>(std::ceil(std::log(0.0001) / std::log(feedback)));
                                const auto burst = capacity * 2; // fill the ring before testing wrapped history
                                const auto end = burst + (capacity + length + 64) * (repeats + 1) + length;
                                bool finite = true;
                                float peak = 0.0f, tailPeak = 0.0f, burstPeak = 0.0f;
                                for (int i = 0; i < end; ++i)
                                {
                                    const auto input = i < burst ? 0.075f * std::sin(static_cast<float>(i) * 0.317f) + 0.025f : 0.0f;
                                    for (int channel = 0; channel < 2; ++channel)
                                    {
                                        const auto wet = engine.processSample(channel, channel == 0 ? input : -input, settings);
                                        finite = finite && std::isfinite(wet);
                                        peak = juce::jmax(peak, std::abs(wet));
                                        if (i < burst) burstPeak = juce::jmax(burstPeak, std::abs(wet));
                                        if (i >= end - length) tailPeak = juce::jmax(tailPeak, std::abs(wet));
                                    }
                                    engine.advance();
                                }
                                const auto label = "length=" + juce::String(length) + " speed=" + juce::String(speed)
                                    + " crossfade=" + juce::String(crossfade) + " feedback=" + juce::String(feedback)
                                    + " offset/random=" + juce::String(modulated ? 1 : 0);
                                expect(finite, label);
                                expectGreaterThan(burstPeak, 0.001f, "Non-vacuous capture: " + label);
                                // Convex feedback cannot exceed inputPeak/(1-feedback). The audible
                                // cubic/equal-power path has at most 1.25*sqrt(2) times that bound.
                                expectLessThan(peak, 0.1f / (1.0f - feedback) * 1.78f, "Feedback gain bound: " + label);
                                expectLessThan(tailPeak, 0.001f, "Silent-input decay: " + label);
                                worstTailPeak = juce::jmax(worstTailPeak, tailPeak);
                                ++cases;
                            }
            logMessage("  feedback matrix cases=" + juce::String(cases)
                       + "; worst final peak=" + juce::String(worstTailPeak, 9));
        }

        beginTest("A quiet sine burst decays by the reported feedback tail");
        {
            ReverseLabAudioProcessor processor;
            setParameter(processor, rl::params::sync, 0.0f);
            setParameter(processor, rl::params::leftFreeMs, 20.0f);
            setParameter(processor, rl::params::feedback, 95.0f);
            setParameter(processor, rl::params::crossfade, 4.0f);
            processor.prepareToPlay(48000.0, 64);
            const auto reportedTail = processor.getTailLengthSeconds();
            const auto end = static_cast<int>(std::ceil(48000.0 * juce::jmax(10.0, 1.1 + reportedTail)));
            double finalEnergy = 0.0;
            float finalPeak = 0.0f;
            int finalSamples = 0;
            bool finite = true;
            juce::MidiBuffer midi;
            for (int start = 0; start < end; start += 64)
            {
                const auto count = juce::jmin(64, end - start);
                juce::AudioBuffer<float> block(2, count);
                for (int i = 0; i < count; ++i)
                {
                    const auto t = start + i;
                    const auto input = t < 48000 ? 0.1f * std::sin(
                        juce::MathConstants<float>::twoPi * 440.0f * static_cast<float>(t) / 48000.0f) : 0.0f;
                    block.setSample(0, i, input); block.setSample(1, i, input);
                }
                processor.processBlock(block, midi);
                for (int i = 0; i < count; ++i)
                {
                    for (int channel = 0; channel < 2; ++channel)
                        finite = std::isfinite(block.getSample(channel, i)) && finite;
                    if (start + i >= end - 4800)
                    {
                        const auto value = block.getSample(0, i);
                        finalEnergy += value * value;
                        finalPeak = juce::jmax(finalPeak, std::abs(value));
                        ++finalSamples;
                    }
                }
            }
            const auto rms = std::sqrt(finalEnergy / static_cast<double>(finalSamples));
            logMessage("  reported tail=" + juce::String(reportedTail) + " s; final RMS="
                       + juce::String(rms, 9) + "; final peak=" + juce::String(finalPeak, 9));
            expect(finite, "Every rendered sample must be finite before peak/RMS reduction");
            expectLessThan(rms, 0.001, "Feedback below unity must decay after the input becomes silent");
            expectLessThan(finalPeak, 0.003f, "The reported tail must not leave a loud sustained output");
        }

        beginTest("Continuous transport is invariant to decreasing and empty block sizes");
        {
            ReverseLabAudioProcessor timed, reference;
            TestPlayHead timedHead, referenceHead;
            for (auto* p : { &timed, &reference })
            {
                setParameter(*p, rl::params::sync, 0.0f);
                setParameter(*p, rl::params::leftFreeMs, 200.0f);
                setParameter(*p, rl::params::crossfade, 0.0f);
            }
            timedHead.position.setIsPlaying(true); referenceHead.position.setIsPlaying(true);
            timedHead.position.setBpm(120.0); referenceHead.position.setBpm(120.0);
            timedHead.position.setTimeInSamples(0);
            timed.setPlayHead(&timedHead); reference.setPlayHead(&referenceHead);
            timed.prepareToPlay(48000.0, 2048); reference.prepareToPlay(48000.0, 2048);
            int64_t position = 0;
            float maxDifference = 0.0f;
            bool finite = true;
            juce::MidiBuffer midi;
            for (int b = 0; b < 60; ++b)
            {
                const std::array<int, 6> varying { 2048, 32, 0, 17, 256, 64 };
                const auto count = b < 15 ? 2048 : varying[static_cast<size_t>(b) % varying.size()];
                juce::AudioBuffer<float> actual(2, count), expected(2, count);
                for (int i = 0; i < count; ++i)
                    for (int channel = 0; channel < 2; ++channel)
                    {
                        const auto input = 0.25f * std::sin(static_cast<float>(position + i) * 0.01f) + 0.25f;
                        actual.setSample(channel, i, input); expected.setSample(channel, i, input);
                    }
                timedHead.position.setTimeInSamples(position);
                timed.processBlock(actual, midi); reference.processBlock(expected, midi);
                for (int i = 0; i < count; ++i)
                {
                    for (int channel = 0; channel < 2; ++channel)
                        finite = std::isfinite(actual.getSample(channel, i))
                                 && std::isfinite(expected.getSample(channel, i)) && finite;
                    maxDifference = juce::jmax(maxDifference,
                        std::abs(actual.getSample(0, i) - expected.getSample(0, i)));
                }
                position += count;
            }
            logMessage("  variable-block/reference maximum difference=" + juce::String(maxDifference, 9));
            expect(finite);
            expectLessThan(maxDifference, 1.0e-6f,
                           "Valid smaller blocks must not reset captured audio or aligned delay lines");
            timed.setPlayHead(nullptr); reference.setPlayHead(nullptr);
        }

        beginTest("A variably partitioned stream matches a fixed 64-sample transport reference");
        {
            constexpr int total = 48000;
            std::array<std::vector<float>, 2> rendered;
            bool finite = true;
            for (size_t pass = 0; pass < rendered.size(); ++pass)
            {
                ReverseLabAudioProcessor processor;
                TestPlayHead head;
                head.position.setIsPlaying(true); head.position.setBpm(120.0);
                head.position.setTimeInSamples(0);
                processor.setPlayHead(&head);
                setParameter(processor, rl::params::sync, 0.0f);
                setParameter(processor, rl::params::leftFreeMs, 200.0f);
                processor.prepareToPlay(48000.0, 2048);
                rendered[pass].reserve(total * 2);
                int position = 0;
                size_t blockIndex = 0;
                juce::MidiBuffer midi;
                while (position < total)
                {
                    const std::array<int, 6> sizes { 2048, 32, 0, 17, 256, 64 };
                    const auto requested = pass == 0 ? sizes[blockIndex++ % sizes.size()] : 64;
                    const auto count = juce::jmin(requested, total - position);
                    juce::AudioBuffer<float> block(2, count);
                    for (int i = 0; i < count; ++i)
                        for (int channel = 0; channel < 2; ++channel)
                            block.setSample(channel, i, 0.2f * std::sin(static_cast<float>(position + i)
                                                                      * (channel == 0 ? 0.017f : 0.029f)));
                    head.position.setTimeInSamples(position);
                    processor.processBlock(block, midi);
                    for (int i = 0; i < count; ++i)
                        for (int channel = 0; channel < 2; ++channel)
                        {
                            const auto value = block.getSample(channel, i);
                            finite = std::isfinite(value) && finite;
                            rendered[pass].push_back(value);
                        }
                    position += count;
                }
                processor.setPlayHead(nullptr);
            }
            expect(finite);
            expectEquals(static_cast<int>(rendered[0].size()), total * 2);
            expectEquals(static_cast<int>(rendered[1].size()), total * 2);
            float maxDifference = 0.0f;
            for (size_t i = 0; i < rendered[0].size(); ++i)
                maxDifference = juce::jmax(maxDifference, std::abs(rendered[0][i] - rendered[1][i]));
            logMessage("  variable/fixed64 maximum difference=" + juce::String(maxDifference, 9));
            expectLessThan(maxDifference, 1.0e-6f);
        }

        beginTest("Reported tails cover slow and fast reverse playback, not only unity speed");
        {
            for (const auto lengthMs : { 20.0f, 75.0f })
                for (const auto speed : { 0.25f, 1.0f, 4.0f })
                {
                    ReverseLabAudioProcessor processor;
                    setParameter(processor, rl::params::sync, 0.0f);
                    setParameter(processor, rl::params::leftFreeMs, lengthMs);
                    setParameter(processor, rl::params::speed, speed);
                    setParameter(processor, rl::params::crossfade, 25.0f);
                    setParameter(processor, rl::params::feedback, 95.0f);
                    processor.prepareToPlay(48000.0, 256);
                    const auto tail = processor.getTailLengthSeconds();
                    const auto burst = 24000;
                    const auto end = burst + static_cast<int>(std::ceil(tail * 48000.0)) + 256;
                    juce::MidiBuffer midi;
                    float afterTailPeak = 0.0f;
                    bool finite = true;
                    for (int start = 0; start < end; start += 256)
                    {
                        const auto count = juce::jmin(256, end - start);
                        juce::AudioBuffer<float> block(2, count);
                        for (int i = 0; i < count; ++i)
                        {
                            const auto t = start + i;
                            const auto input = t < burst ? 0.05f * std::sin(static_cast<float>(t) * 0.137f) + 0.05f : 0.0f;
                            block.setSample(0, i, input); block.setSample(1, i, -input);
                        }
                        processor.processBlock(block, midi);
                        for (int i = 0; i < count; ++i)
                        {
                            for (int channel = 0; channel < 2; ++channel)
                                finite = std::isfinite(block.getSample(channel, i)) && finite;
                            if (start >= end - 512)
                                afterTailPeak = juce::jmax(afterTailPeak, std::abs(block.getSample(0, i)));
                        }
                    }
                    logMessage("  length=" + juce::String(lengthMs) + " ms; speed=" + juce::String(speed)
                               + "; reported tail=" + juce::String(tail) + "; final peak=" + juce::String(afterTailPeak, 9));
                    expect(finite);
                    expectLessThan(afterTailPeak, 0.001f);
                }
        }

        beginTest("Frozen processor output stays input-independent through tempo and length automation");
        {
            ReverseLabAudioProcessor a, b;
            TestPlayHead head;
            head.position.setIsPlaying(true); head.position.setBpm(120.0);
            for (auto* p : { &a, &b })
            {
                setParameter(*p, rl::params::leftSize, 0.0f);
                setParameter(*p, rl::params::freeze, 1.0f);
                p->setPlayHead(&head); p->prepareToPlay(48000.0, 64);
            }
            float maximumDifference = 0.0f;
            double captureEnergy = 0.0;
            bool finite = true;
            juce::MidiBuffer midi;
            for (int n = 0; n < 800; ++n)
            {
                head.position.setTimeInSamples(static_cast<int64_t>(n) * 64);
                head.position.setBpm(n < 200 ? 120.0 : n < 400 ? 60.0 : n < 600 ? 90.0 : 180.0);
                if (n == 300 || n == 500)
                    for (auto* p : { &a, &b })
                        setParameter(*p, rl::params::leftSize, n == 300 ? 5.0f : 2.0f);
                juce::AudioBuffer<float> x(2, 64), y(2, 64);
                for (int i = 0; i < 64; ++i)
                    for (int channel = 0; channel < 2; ++channel)
                    {
                        const auto original = 0.2f * std::sin(static_cast<float>(n * 64 + i) * 0.05f);
                        x.setSample(channel, i, n < 150 ? original : 0.0f);
                        y.setSample(channel, i, n < 150 ? original : -0.5f);
                    }
                a.processBlock(x, midi); b.processBlock(y, midi);
                a.servicePendingHostUpdatesForTesting(); b.servicePendingHostUpdatesForTesting();
                for (int i = 0; i < 64; ++i)
                {
                    for (int channel = 0; channel < 2; ++channel)
                        finite = std::isfinite(x.getSample(channel, i)) && std::isfinite(y.getSample(channel, i)) && finite;
                    if (n >= 150)
                        maximumDifference = juce::jmax(maximumDifference, std::abs(x.getSample(0, i) - y.getSample(0, i)));
                    else
                        captureEnergy += x.getSample(0, i) * x.getSample(0, i);
                }
            }
            expect(finite);
            expectGreaterThan(captureEnergy, 1.0);
            expectLessThan(maximumDifference, 1.0e-6f);
            a.setPlayHead(nullptr); b.setPlayHead(nullptr);
        }

        beginTest("Maximum feedback tail is finite and not silently capped below its history bound");
        {
            for (const auto sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
            {
                ReverseLabAudioProcessor processor;
                TestPlayHead head;
                head.position.setBpm(20.0);
                head.position.setTimeSignature(juce::AudioPlayHead::TimeSignature { 4, 4 });
                processor.setPlayHead(&head);
                setParameter(processor, rl::params::leftSize, 14.0f);
                setParameter(processor, rl::params::feedback, 95.0f);
                setParameter(processor, rl::params::speed, 4.0f);
                setParameter(processor, rl::params::stereoOffset, 100.0f);
                setParameter(processor, rl::params::random, 100.0f);
                processor.prepareToPlay(sampleRate, 256);
                const auto tail = processor.getTailLengthSeconds();
                expect(std::isfinite(tail));
                expectWithinAbsoluteError(tail, 5808.281 + 2896.0 / sampleRate, 0.001);
                logMessage("  maximum tail at " + juce::String(sampleRate) + " Hz=" + juce::String(tail, 6) + " s");
                processor.setPlayHead(nullptr);
            }
        }

        beginTest("An engaged Freeze cannot recapture when requested lengths increase");
        {
            rl::ReverseEngine left, right;
            left.prepare(48000.0, 48000); right.prepare(48000.0, 48000);
            rl::EngineSettings settings;
            settings.leftLength = settings.rightLength = 9600;
            settings.freeze = true;
            bool finite = true;
            for (int i = 0; i < 20000; ++i)
            {
                const auto input = 0.2f * std::sin(static_cast<float>(i) * 0.09f);
                for (auto* engine : { &left, &right })
                {
                    const auto wetLeft = engine->processSample(0, input, settings);
                    const auto wetRight = engine->processSample(1, input, settings);
                    finite = std::isfinite(wetLeft) && std::isfinite(wetRight) && finite;
                    engine->advance();
                }
            }
            expect(left.isHoldingFrozenCapture());
            const auto heldWriter = left.getWritePosition();
            bool heldThroughout = true;
            float maxDifference = 0.0f;
            for (int i = 0; i < 48000 * 8; ++i) // eight complete ring durations, with writing stopped
            {
                // The same length changes represent manual/free-time automation or slower host tempo.
                settings.leftLength = i < 20000 ? 19200 : 24000;
                settings.rightLength = i < 20000 ? 14400 : 12000;
                const auto a = left.processSample(0, 0.0f, settings);
                const auto b = right.processSample(0, -0.5f, settings);
                const auto aRight = left.processSample(1, 0.0f, settings);
                const auto bRight = right.processSample(1, 0.5f, settings);
                finite = std::isfinite(a) && std::isfinite(b)
                         && std::isfinite(aRight) && std::isfinite(bRight) && finite;
                left.advance(); right.advance();
                heldThroughout = heldThroughout && left.isHoldingFrozenCapture()
                                && left.getWritePosition() == heldWriter;
                maxDifference = juce::jmax(maxDifference, std::abs(a - b));
            }
            logMessage("  frozen input-independence maximum difference=" + juce::String(maxDifference, 9));
            expect(finite);
            expect(heldThroughout, "Freeze must remain latched until Freeze is disabled or explicitly reset");
            expectLessThan(maxDifference, 1.0e-6f, "Length changes must not admit new input into a held capture");
            settings.freeze = false;
            const auto unfreezeLeft = left.processSample(0, 0.1f, settings);
            const auto unfreezeRight = left.processSample(1, 0.1f, settings);
            finite = std::isfinite(unfreezeLeft) && std::isfinite(unfreezeRight) && finite;
            left.advance();
            expect(!left.isHoldingFrozenCapture());
            expect(left.getWritePosition() != heldWriter, "Explicit Unfreeze must resume capture");
            float resumedMinimum = 10.0f, resumedMaximum = -10.0f;
            for (int i = 0; i < 96000; ++i)
            {
                const auto input = 0.2f * std::sin(static_cast<float>(i) * 0.04f);
                const auto output = left.processSample(0, input, settings);
                const auto other = left.processSample(1, input, settings);
                finite = std::isfinite(output) && std::isfinite(other) && finite;
                left.advance();
                if (i >= 72000)
                {
                    resumedMinimum = juce::jmin(resumedMinimum, output);
                    resumedMaximum = juce::jmax(resumedMaximum, output);
                }
            }
            expect(finite);
            expectGreaterThan(resumedMaximum - resumedMinimum, 0.1f,
                              "After a long Freeze, Unfreeze must resume changing, newly captured audio");
        }
    }
};

class StateTransactionTests final : public juce::UnitTest
{
public:
    StateTransactionTests() : UnitTest("Control state transactions", "StateTransactions") {}

    void runTest() override
    {
        beginTest("The first and every parameter callback capture the complete new state, never a partial preset");
        // Explicit per-run storage even though these tests never invoke a file operation.
        const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("reverselab-state-" + juce::Uuid().toString());
        ReverseLabAudioProcessor target(root.getChildFile("target"));
        target.setCurrentProgram(3);
        ReverseLabAudioProcessor y(root.getChildFile("source-y")), z(root.getChildFile("source-z"));
        makeCustomState(y, 2, false);
        makeCustomState(z, 1, true);
        const auto stateY = snapshot(y), stateZ = snapshot(z);
        expect(stateTree(stateY).getChildWithName("WkPresetSelection").isValid(),
               "The fixture must exercise user-selection metadata as well as all 19 parameters");

        {
            ReverseLabAudioProcessor processor(root.getChildFile("capture"));
            makeCustomState(processor, 0, false);
            const auto before = snapshot(processor);
            const auto beforeTree = stateTree(before);
            target.setLastEditorSize(static_cast<int>(beforeTree["editorWidth"]), static_cast<int>(beforeTree["editorHeight"]));
            const auto completeProgram = snapshot(target);
            std::vector<juce::MemoryBlock> captured;
            int firstIndex = -1, programNoticesBeforeFirst = -1;
            ParameterCallbackHost host(processor, [&](int index)
            {
                if (captured.empty()) { firstIndex = index; programNoticesBeforeFirst = host.programNotifications; }
                captured.push_back(snapshot(processor));
            });
            processor.setCurrentProgram(3);
            expectEquals(firstIndex, processor.parameters.getParameter(rl::params::bypass)->getParameterIndex());
            expectEquals(programNoticesBeforeFirst, 0);
            expectEquals(static_cast<int>(captured.size()), 19);
            for (const auto& state : captured)
                expect(sameCompleteState(processor, state, completeProgram), "Commit precedes notifications: every callback sees the exact complete new program");
            // Preserve the dimensions across factory changes when comparing the committed state.
            expect(sameCompleteState(processor, snapshot(processor), snapshot(target))
                       && liveStateMatches(processor, snapshot(target)), "The completed preset must be fully published and live");
            ReverseLabAudioProcessor recalled(root.getChildFile("capture-recalled"));
            if (! captured.empty()) restore(recalled, captured.front());
            expect(sameCompleteState(recalled, snapshot(recalled), completeProgram) && liveStateMatches(recalled, completeProgram),
                   "A fresh processor recalls the same complete callback snapshot into its live parameters and metadata");
        }

        beginTest("A state restored in the first parameter callback wins over the old preset loop");
        {
            ReverseLabAudioProcessor processor(root.getChildFile("restore"));
            int actions = 0, firstIndex = -1, programNoticesBeforeFirst = -1;
            ParameterCallbackHost host(processor, [&](int index)
            {
                if (actions != 0) return;
                ++actions;
                firstIndex = index;
                programNoticesBeforeFirst = host.programNotifications;
                restore(processor, stateY);
                expect(sameCompleteState(processor, snapshot(processor), stateY) && liveStateMatches(processor, stateY, false),
                       "Restore must return with all authoritative parameters and metadata committed, before old callbacks finish");
            });
            processor.setCurrentProgram(3);
            expectEquals(actions, 1);
            expectEquals(firstIndex, processor.parameters.getParameter(rl::params::bypass)->getParameterIndex());
            expectEquals(programNoticesBeforeFirst, 0);
            expectEquals(host.maximumDepth, 1, "Only notifications are deferred; the restore itself commits without recursive listener traversal");
            expect(sameCompleteState(processor, snapshot(processor), stateY) && liveStateMatches(processor, stateY),
                   "All 19 live parameters, program, dimensions and selection belong to restored Y");
            processor.servicePendingHostUpdatesForTesting();
            expect(sameCompleteState(processor, snapshot(processor), stateY) && liveStateMatches(processor, stateY),
                   "No stale request may undo the completed restore on the next tick");
        }

        beginTest("Two nested restores each commit synchronously; only their notifications may coalesce");
        {
            ReverseLabAudioProcessor processor(root.getChildFile("two-restores"));
            target.setLastEditorSize(900, 610);
            const auto completeProgram = snapshot(target);
            juce::MemoryBlock afterFirst, afterSecond;
            bool firstLive = false, secondLive = false;
            std::vector<juce::MemoryBlock> callbackStates;
            int actions = 0;
            ParameterCallbackHost host(processor, [&](int)
            {
                callbackStates.push_back(snapshot(processor));
                if (actions != 0) return;
                ++actions;
                restore(processor, stateY);
                afterFirst = snapshot(processor);
                firstLive = liveStateMatches(processor, stateY, false);
                restore(processor, stateZ);
                afterSecond = snapshot(processor);
                secondLive = liveStateMatches(processor, stateZ, false);
            });
            processor.setCurrentProgram(3);
            expectEquals(actions, 1);
            expectEquals(host.maximumDepth, 1);
            expect(sameCompleteState(processor, afterFirst, stateY) && firstLive, "The first nested restore must actually commit Y before returning");
            expect(sameCompleteState(processor, afterSecond, stateZ) && secondLive, "The second nested restore must actually commit Z before returning");
            expect(sameCompleteState(processor, snapshot(processor), stateZ) && liveStateMatches(processor, stateZ),
                   "The second complete restore must survive the outer preset loop in both the snapshot and live state");
            bool sawProgramCommit = false, sawFinalRestoreCommit = false;
            for (const auto& captured : callbackStates)
            {
                const auto isProgram = sameCompleteState(processor, captured, completeProgram);
                const auto isFinalRestore = sameCompleteState(processor, captured, stateZ);
                sawProgramCommit = sawProgramCommit || isProgram;
                sawFinalRestoreCommit = sawFinalRestoreCommit || isFinalRestore;
                expect(isProgram || isFinalRestore,
                       "Notifications observe only full currently committed states; obsolete Y notifications may coalesce");
            }
            expect(sawProgramCommit && sawFinalRestoreCommit, "The superseded generation must abort and actually replay the latest generation");
        }

        beginTest("A state-restore callback can request another state without a recursive APVTS traversal");
        {
            ReverseLabAudioProcessor processor(root.getChildFile("restore-from-restore"));
            int actions = 0;
            ParameterCallbackHost host(processor, [&](int)
            {
                if (actions++ == 0) restore(processor, stateZ);
            });
            restore(processor, stateY);
            expectGreaterThan(actions, 0);
            expectEquals(host.maximumDepth, 1);
            expect(sameCompleteState(processor, snapshot(processor), stateZ) && liveStateMatches(processor, stateZ));
        }

        beginTest("A later same-program request is not an echo when an earlier restore is pending");
        {
            ReverseLabAudioProcessor processor(root.getChildFile("restore-then-program"));
            bool action = false;
            ParameterCallbackHost host(processor, [&](int)
            {
                if (action) return;
                action = true;
                restore(processor, stateY);
                processor.setCurrentProgram(3); // A later explicit request, after Y has synchronously committed program 2.
            });
            processor.setCurrentProgram(3);
            expect(action);
            expect(sameCompleteState(processor, snapshot(processor), stateY) && liveStateMatches(processor, stateY),
                   "The state request drains synchronously before the deferred program request");
            processor.servicePendingHostUpdatesForTesting();
            const auto expectedSize = stateTree(stateY);
            target.setLastEditorSize(static_cast<int>(expectedSize["editorWidth"]), static_cast<int>(expectedSize["editorHeight"]));
            const auto expected = snapshot(target);
            expect(sameCompleteState(processor, snapshot(processor), expected) && liveStateMatches(processor, expected),
                   "The later explicit program request must survive the echo guard and clear user selection on the next tick");
        }

        beginTest("A superseded program notification cannot let a host echo undo the queued restore");
        {
            ReverseLabAudioProcessor processor(root.getChildFile("restore-with-program-echo"));
            EchoingProgramHost echo(processor);
            processor.addListener(&echo);
            const juce::ScopeGuard removeEcho { [&] { processor.removeListener(&echo); } };
            bool action = false;
            ParameterCallbackHost host(processor, [&](int)
            {
                if (action) return;
                action = true;
                restore(processor, stateY);
            });
            processor.setCurrentProgram(3);
            expect(action);
            expect(sameCompleteState(processor, snapshot(processor), stateY) && liveStateMatches(processor, stateY));
            processor.servicePendingHostUpdatesForTesting();
            processor.servicePendingHostUpdatesForTesting();
            expectEquals(echo.programNotifications, 0, "A program already superseded by a queued restore must not be advertised to an echoing host");
            expect(sameCompleteState(processor, snapshot(processor), stateY) && liveStateMatches(processor, stateY),
                   "The queued restored sound must survive ordinary program notification echoes on later ticks");
        }

        beginTest("A restore cascade beyond one work quantum completes on later ticks without recursion or lost work");
        {
            ReverseLabAudioProcessor processor(root.getChildFile("bounded-echo"));
            int lastObservedProgram = -1, requests = 0;
            ParameterCallbackHost host(processor, [&](int)
            {
                const auto program = processor.getCurrentProgram();
                if (program == lastObservedProgram) return;
                lastObservedProgram = program;
                // A test-side safety cap also keeps the unfixed implementation
                // finite; it is deliberately greater than the processor's budget.
                if (++requests < 96) restore(processor, program == 2 ? stateZ : stateY);
            });
            processor.setCurrentProgram(3);
            const auto firstDrainSends = host.parameterNotifications;
            expectGreaterThan(requests, 2, "The control must exercise an actual alternating cascade");
            expect(requests <= 64, "One outer operation cannot drain an unlimited listener-generated restore chain");
            expect(firstDrainSends <= 64, "A single drain must bound actual parameter sends, not merely restore requests");
            expect(processor.hasPendingStateNotificationsForTesting(), "The finite cascade must exceed the first drain's quantum");
            expectEquals(host.maximumDepth, 1);
            for (int tick = 0; tick < 8 && requests < 96; ++tick)
                processor.servicePendingHostUpdatesForTesting();
            expectEquals(requests, 96, "Bounded work must resume and finish the finite cascade, not silently discard remaining restores");
            processor.servicePendingHostUpdatesForTesting();
            const auto finalState = snapshot(processor);
            // Request 1 observes program3; even requests observe Y and odd ones Z.
            // Request96 therefore ends exactly at Y, not either plausible state.
            expect(sameCompleteState(processor, finalState, stateY) && liveStateMatches(processor, stateY),
                   "The exact last request must win in all 19 values, program, size, selection and APVTS caches");
            expect(! processor.hasPendingStateNotificationsForTesting(), "The final replay must consume the pending generation");
            const auto finishedSends = host.parameterNotifications;
            processor.servicePendingHostUpdatesForTesting();
            expectEquals(requests, 96, "A later service tick must not resurrect a superseded generation");
            expectEquals(host.parameterNotifications, finishedSends, "A fully drained generation must not send again");
            logMessage("  first-drain parameter sends=" + juce::String(firstDrainSends)
                       + "; completed requests=" + juce::String(requests)
                       + "; final state=Y; pending=" + juce::String(processor.hasPendingStateNotificationsForTesting() ? 1 : 0));
        }

        beginTest("Independent control writers and a snapshot reader cannot publish mixed states");
        {
            ReverseLabAudioProcessor processor(root.getChildFile("concurrent-restores"));
            restore(processor, stateY);
            std::atomic<bool> start { false };
            std::vector<juce::MemoryBlock> captured;
            const auto write = [&](const juce::MemoryBlock& state)
            {
                while (! start.load(std::memory_order_acquire)) std::this_thread::yield();
                for (int i = 0; i < 64; ++i) restore(processor, state);
            };
            std::thread firstWriter(write, std::cref(stateY));
            std::thread secondWriter(write, std::cref(stateZ));
            std::thread reader([&]
            {
                while (! start.load(std::memory_order_acquire)) std::this_thread::yield();
                for (int i = 0; i < 256; ++i) captured.push_back(snapshot(processor));
            });
            start.store(true, std::memory_order_release);
            firstWriter.join(); secondWriter.join(); reader.join();
            processor.servicePendingHostUpdatesForTesting();
            expectEquals(static_cast<int>(captured.size()), 256);
            captured.push_back(snapshot(processor));
            for (const auto& state : captured)
                expect(sameCompleteState(processor, state, stateY) || sameCompleteState(processor, state, stateZ),
                       "Concurrent capture may choose either whole state, never a mixture of program/parameters/size/selection");
            expect(liveStateMatches(processor, stateY) || liveStateMatches(processor, stateZ),
                   "The final live state must independently match a complete writer, not only a cached snapshot");
        }

        beginTest("An independent restore commits and returns while an older parameter callback is still active");
        {
            ReverseLabAudioProcessor processor(root.getChildFile("overlapping-controls"));
            juce::WaitableEvent callbackEntered, writerFinished;
            std::atomic<bool> writerReturned { false }, writerTimedOut { false };
            juce::MemoryBlock captured;
            std::thread writer([&]
            {
                if (! callbackEntered.wait(2000)) { writerTimedOut.store(true); return; }
                restore(processor, stateY);
                writerReturned.store(true);
                writerFinished.signal();
            });
            bool action = false;
            ParameterCallbackHost host(processor, [&](int)
            {
                if (action) return;
                action = true;
                callbackEntered.signal();
                expect(writerFinished.wait(2000), "The writer must return while the old callback remains active, not wait on its listener lock");
                expect(liveStateMatches(processor, stateY, false), "Authoritative live Y is observable before releasing the old callback");
                std::thread reader([&] { captured = snapshot(processor); });
                reader.join();
                expect(writerReturned.load());
            });
            processor.setCurrentProgram(3);
            writer.join();
            expect(action && ! writerTimedOut.load() && writerReturned.load());
            expect(sameCompleteState(processor, captured, stateY));
            expect(sameCompleteState(processor, snapshot(processor), stateY) && liveStateMatches(processor, stateY),
                   "The committed restore remains intact and all APVTS caches converge after the old notification returns");
        }

        beginTest("Malformed parameter records are rejected atomically before DSP state changes");
        {
            ReverseLabAudioProcessor baselineSource(root.getChildFile("invalid-state-source"));
            makeCustomState(baselineSource, 2, false);
            setParameter(baselineSource, rl::params::sync, 0.0f);
            setParameter(baselineSource, rl::params::link, 1.0f);
            setParameter(baselineSource, rl::params::leftFreeMs, 20.0f);
            setParameter(baselineSource, rl::params::rightFreeMs, 20.0f);
            setParameter(baselineSource, rl::params::speed, 1.0f);
            setParameter(baselineSource, rl::params::mix, 100.0f);
            setParameter(baselineSource, rl::params::feedback, 0.0f);
            setParameter(baselineSource, rl::params::random, 0.0f);
            setParameter(baselineSource, rl::params::stereoOffset, 0.0f);
            setParameter(baselineSource, rl::params::bypass, 0.0f);
            const auto baseline = snapshot(baselineSource);

            const auto withValue = [&](const char* id, const char* text)
            {
                auto tree = stateTree(baseline);
                // Every malformed fixture also carries valid-but-different control
                // metadata, so rejection cannot pass after a partial program,
                // editor-size or selection commit.
                tree.setProperty("program", 1, nullptr);
                tree.setProperty("editorWidth", 1180, nullptr);
                tree.setProperty("editorHeight", 780, nullptr);
                if (auto selection = tree.getChildWithName("WkPresetSelection"); selection.isValid())
                    selection.setProperty("name", "Must Not Commit", nullptr);
                auto parameter = tree.getChildWithProperty("id", id);
                expect(parameter.isValid(), "The negative-state fixture must address an existing parameter");
                parameter.setProperty("value", juce::String(text), nullptr);
                juce::MemoryBlock result;
                if (auto xml = tree.createXml()) juce::AudioProcessor::copyXmlToBinary(*xml, result);
                return result;
            };
            struct InvalidState { juce::String name; juce::MemoryBlock bytes; };
            std::vector<InvalidState> invalidStates;
            invalidStates.push_back({ "NaN speed", withValue(rl::params::speed, "nan") });
            invalidStates.push_back({ "+Inf feedback", withValue(rl::params::feedback, "inf") });
            invalidStates.push_back({ "-Inf mix", withValue(rl::params::mix, "-inf") });
            invalidStates.push_back({ "non-numeric mix", withValue(rl::params::mix, "not-a-number") });
            invalidStates.push_back({ "below-range free time", withValue(rl::params::leftFreeMs, "19.9") });
            invalidStates.push_back({ "above-range free time", withValue(rl::params::leftFreeMs, "4000.1") });
            {
                auto tree = stateTree(baseline);
                tree.setProperty("program", 1, nullptr);
                tree.setProperty("editorWidth", 1180, nullptr);
                tree.setProperty("editorHeight", 780, nullptr);
                juce::ValueTree masqueradingExtension("Future");
                masqueradingExtension.setProperty("id", rl::params::output, nullptr);
                masqueradingExtension.setProperty("value", -6.0, nullptr);
                tree.addChild(masqueradingExtension, -1, nullptr);
                juce::MemoryBlock bytes;
                if (auto xml = tree.createXml()) juce::AudioProcessor::copyXmlToBinary(*xml, bytes);
                invalidStates.push_back({ "non-PARAM child with a known parameter ID", std::move(bytes) });
            }

            for (size_t caseIndex = 0; caseIndex < invalidStates.size(); ++caseIndex)
            {
                const auto& invalid = invalidStates[caseIndex];
                ReverseLabAudioProcessor processor(root.getChildFile("invalid-state-subject-" + juce::String(caseIndex)));
                ReverseLabAudioProcessor reference(root.getChildFile("invalid-state-reference-" + juce::String(caseIndex)));
                restore(processor, baseline);
                restore(reference, baseline);
                processor.prepareToPlay(48000.0, 64);
                reference.prepareToPlay(48000.0, 64);

                int position = 0;
                float maximumDifference = 0.0f;
                bool finite = true;
                double comparedEnergy = 0.0;
                const auto renderPair = [&](int blocks, bool measureEnergy)
                {
                    juce::MidiBuffer midi;
                    for (int blockIndex = 0; blockIndex < blocks; ++blockIndex)
                    {
                        juce::AudioBuffer<float> actual(2, 64), expected(2, 64);
                        for (int sample = 0; sample < 64; ++sample)
                            for (int channel = 0; channel < 2; ++channel)
                            {
                                const auto value = 0.2f * std::sin(static_cast<float>(position + sample)
                                                                  * (channel == 0 ? 0.071f : 0.113f));
                                actual.setSample(channel, sample, value);
                                expected.setSample(channel, sample, value);
                            }
                        processor.processBlock(actual, midi);
                        reference.processBlock(expected, midi);
                        for (int sample = 0; sample < 64; ++sample)
                            for (int channel = 0; channel < 2; ++channel)
                            {
                                const auto a = actual.getSample(channel, sample);
                                const auto b = expected.getSample(channel, sample);
                                finite = finite && std::isfinite(a) && std::isfinite(b);
                                maximumDifference = juce::jmax(maximumDifference, std::abs(a - b));
                                if (measureEnergy) comparedEnergy += static_cast<double>(b) * static_cast<double>(b);
                            }
                        position += 64;
                    }
                };

                renderPair(48, false); // Populate real reverse history before attempting the rejected restore.
                const auto before = snapshot(processor);
                const auto notificationsPendingBefore = processor.hasPendingStateNotificationsForTesting();
                restore(processor, invalid.bytes);
                const auto after = snapshot(processor);
                const auto rejected = sameCompleteState(processor, after, before)
                                   && liveStateMatches(processor, before)
                                   && processor.hasPendingStateNotificationsForTesting() == notificationsPendingBefore;
                expect(rejected, invalid.name + " must leave all parameters, program, editor, selection and notifications unchanged");

                // In particular, a NaN speed must never reach ReverseEngine::readInterpolated(),
                // where the read position is converted to an integer. Rendering also proves that
                // a rejected state did not publish a hidden DSP-reset generation.
                if (rejected) renderPair(8, true);
                expect(finite, invalid.name + " must not produce a non-finite audio sample");
                expectGreaterThan(comparedEnergy, 0.01, invalid.name + " must exercise audible running DSP");
                expectWithinAbsoluteError(maximumDifference, 0.0f, 0.0f,
                                          invalid.name + " must not alter or reset the running DSP");
            }
        }

        beginTest("Frozen legacy fixtures keep missing-default, present-default and unknown-extension semantics");
        {
            ReverseLabAudioProcessor processor(root.getChildFile("legacy-state"));
            restore(processor, stateY);
            expectWithinAbsoluteError(processor.parameters.getRawParameterValue(rl::params::crossfade)->load(), 11.0f, 1.0e-6f,
                                      "Fixture must start from a non-default value before testing missing-child recall");
            // Fixed XML, not generated by the serializer under test. The archived
            // original APVTS restores absent crossfade to DEFAULT4 (childAdded),
            // defaults present-but-valueless mix to100, and uses last duplicate -7.
            const auto xml = juce::parseXML(R"(<ReverseLabState program="1" future="v2">
                <PARAM id="mix"/><PARAM id="output" value="-3"/>
                <PARAM id="output" value="-7" extra="retained"/>
                <PARAM id="futureParameter" value="123" forward="retained"/>
                <Future payload="untouched"/>
                </ReverseLabState>)");
            juce::MemoryBlock legacy;
            juce::AudioProcessor::copyXmlToBinary(*xml, legacy);
            restore(processor, legacy);
            const auto plain = [&](const char* id)
            {
                const auto* parameter = processor.parameters.getParameter(id);
                return parameter->convertFrom0to1(parameter->getValue());
            };
            expectWithinAbsoluteError(plain(rl::params::crossfade), 4.0f, 1.0e-6f);
            expectWithinAbsoluteError(plain(rl::params::mix), 100.0f, 1.0e-6f);
            expectWithinAbsoluteError(plain(rl::params::output), -7.0f, 1.0e-6f);
            expectEquals(processor.getCurrentProgram(), 1);
            expect(processor.getLastEditorSize() == juce::Point<int>(900, 610));
            const auto saved = stateTree(snapshot(processor));
            expectEquals(saved["future"].toString(), juce::String("v2"));
            expectEquals(saved.getChildWithName("Future")["payload"].toString(), juce::String("untouched"));
            expectEquals(saved.getChildWithProperty("id", "futureParameter")["forward"].toString(),
                         juce::String("retained"));
            expectEquals(saved.getChild(2)["extra"].toString(), juce::String("retained"));
            expectWithinAbsoluteError(static_cast<float>(saved.getChild(1)["value"]), -3.0f, 1.0e-6f);
            processor.setCurrentProgram(3);
            const auto factory = stateTree(snapshot(processor));
            expectEquals(factory["future"].toString(), juce::String("v2"));
            expect(saved.getChildWithName("Future").isEquivalentTo(factory.getChildWithName("Future")));
            expect(saved.getChildWithProperty("id", "futureParameter")
                        .isEquivalentTo(factory.getChildWithProperty("id", "futureParameter")));
            expectEquals(factory.getChild(2)["extra"].toString(), juce::String("retained"));
            ReverseLabAudioProcessor recalled(root.getChildFile("legacy-recalled"));
            restore(recalled, snapshot(processor));
            for (const auto& [id, ignored] : factoryBank().front().values)
            {
                juce::ignoreUnused(ignored);
                const auto* expected = processor.parameters.getParameter(id);
                const auto* actual = recalled.parameters.getParameter(id);
                expectWithinAbsoluteError(actual->getValue(), expected->getValue(), 1.0e-6f, id);
            }
        }

        beginTest("Preparing at different sample rates preserves the complete non-default user state");
        {
            ReverseLabAudioProcessor processor(root.getChildFile("prepare-state"));
            restore(processor, stateY);
            setParameter(processor, rl::params::freeze, 1.0f);
            const auto expected = snapshot(processor);
            for (const auto rate : { 44100.0, 96000.0 })
            {
                processor.prepareToPlay(rate, 128);
                expect(sameCompleteState(processor, snapshot(processor), expected) && liveStateMatches(processor, expected),
                       "prepareToPlay may reset DSP history, but not the 19 values, selection, program or dimensions");
                processor.releaseResources();
            }
        }

        beginTest("Worker program requests remain deferred and state restores cancel only older requests");
        {
            ReverseLabAudioProcessor processor(root.getChildFile("deferred-programs"));
            std::thread programWriter([&] { processor.setCurrentProgram(3); });
            programWriter.join();
            expectEquals(processor.getCurrentProgram(), 0, "Worker setters must not apply factory values before the message-thread service");
            processor.servicePendingHostUpdatesForTesting();
            expectEquals(processor.getCurrentProgram(), 3);
            std::thread olderRequest([&] { processor.setCurrentProgram(1); });
            olderRequest.join();
            restore(processor, stateY);
            processor.servicePendingHostUpdatesForTesting();
            expect(sameCompleteState(processor, snapshot(processor), stateY) && liveStateMatches(processor, stateY),
                   "A restore supersedes an older unapplied program request");
            int requests = 0;
            ParameterCallbackHost host(processor, [&](int)
            {
                if (requests++ == 0)
                {
                    processor.setCurrentProgram(processor.getCurrentProgram()); // callback echo
                    processor.setCurrentProgram(3); // distinct request made after this restore started
                }
            });
            std::thread restoreWriter([&] { restore(processor, stateZ); });
            restoreWriter.join();
            expectGreaterThan(requests, 0);
            expect(sameCompleteState(processor, snapshot(processor), stateZ) && liveStateMatches(processor, stateZ),
                   "A nested worker-thread program request remains deferred");
            processor.servicePendingHostUpdatesForTesting();
            expectEquals(processor.getCurrentProgram(), 3, "A new request from the restore's callback must survive to the next tick");
        }
    }
};

class ListenerLockRegressionTests final : public juce::UnitTest
{
    struct HeldListenerHost final : juce::AudioProcessorListener
    {
        HeldListenerHost(ReverseLabAudioProcessor& owner, const juce::MemoryBlock& expected)
            : processor(owner), state(expected), a(std::this_thread::get_id()) { processor.addListener(this); }
        ~HeldListenerHost() override { processor.removeListener(this); }

        bool awaitSetup(juce::WaitableEvent& event)
        {
            if (event.wait(2000)) return true;
            setupFailed.store(true);
            // Release setup waits so a missed prerequisite is a test failure,
            // not mistaken for the product deadlock guarded by CTest's timeout.
            startB.signal(); bHoldingMix.signal(); allowRestore.signal();
            return false;
        }

        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChanged(juce::AudioProcessor*, int index, float) override
        {
            const auto onA = std::this_thread::get_id() == a;
            const auto bypass = processor.parameters.getParameter(rl::params::bypass)->getParameterIndex();
            const auto crossfade = processor.parameters.getParameter(rl::params::crossfade)->getParameterIndex();
            const auto mix = processor.parameters.getParameter(rl::params::mix)->getParameterIndex();
            if (onA && index == bypass && ! aStarted.exchange(true))
            {
                startB.signal();
                static_cast<void>(awaitSetup(bHoldingMix));
            }
            else if (onA && index == crossfade && ! restoreAllowed.exchange(true))
            {
                // B already owns JUCE's mix listener lock. A has left bypass;
                // after this signal A never waits or joins inside a callback.
                std::fprintf(stderr, "[listener-lock] A advanced to crossfade; B owns mix listener lock\n");
                allowRestore.signal();
            }
            else if (! onA && index == mix && ! bStarted.exchange(true))
            {
                bHoldingMix.signal();
                if (! awaitSetup(allowRestore) || setupFailed.load()) return;
                std::fprintf(stderr, "[listener-lock] B calls restore while holding JUCE mix listener lock\n");
                restore(processor, state);
                immediateState = snapshot(processor);
                immediateLive.store(liveStateMatches(processor, state, false) && ! processor.presets.isModified());
                bReturned.store(true);
                std::fprintf(stderr, "[listener-lock] B restore returned with authoritative state committed\n");
            }
            if (onA && index == mix) aReachedMix.store(true);
        }

        ReverseLabAudioProcessor& processor;
        const juce::MemoryBlock& state;
        const std::thread::id a;
        juce::WaitableEvent startB, bHoldingMix, allowRestore;
        juce::MemoryBlock immediateState; // written by B, read only after its join
        std::atomic<bool> aStarted { false }, restoreAllowed { false }, bStarted { false }, bReturned { false },
                          aReachedMix { false }, setupFailed { false }, immediateLive { false };
    };

public:
    ListenerLockRegressionTests() : UnitTest("Foreign parameter-listener lock regression", "ListenerLock") {}

    void runTest() override
    {
        beginTest("A program notification and B held mix-listener restore cannot form an ABBA deadlock");
        const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("reverselab-listener-lock-" + juce::Uuid().toString());
        ReverseLabAudioProcessor y(root.getChildFile("source-y"));
        makeCustomState(y, 2, false);
        auto expected = stateTree(snapshot(y));
        // Match the original independent probe's B automation37 / restoredY53,
        // while additionally exercising complete user-selection metadata.
        expected.getChildWithProperty("id", rl::params::mix).setProperty("value", 53.0f, nullptr);
        expected.getChildWithName("WkPresetSelection").getChildWithProperty("id", rl::params::mix)
            .setProperty("value", 53.0f, nullptr);
        juce::MemoryBlock stateY;
        juce::AudioProcessor::copyXmlToBinary(*expected.createXml(), stateY);
        ReverseLabAudioProcessor processor(root.getChildFile("target"));
        HeldListenerHost host(processor, stateY);
        std::thread b([&]
        {
            if (host.awaitSetup(host.startB)) setParameter(processor, rl::params::mix, 37.0f);
        });
        processor.setCurrentProgram(3);
        b.join(); // outside every callback and after the entire program setter

        expect(! host.setupFailed.load(), "Both real parameter callbacks must reach the prescribed lock-order setup");
        expect(host.aStarted.load() && host.restoreAllowed.load() && host.bStarted.load()
                   && host.bReturned.load() && host.aReachedMix.load(), "The actual held-listener overlap must execute and complete");
        expect(host.immediateLive.load() && sameCompleteState(processor, host.immediateState, stateY),
               "B's restore must synchronously publish all19 ranged values, program, dimensions and user metadata before its callback returns");
        expect(sameCompleteState(processor, snapshot(processor), stateY) && liveStateMatches(processor, stateY),
               "After the actual A notification replay, all19 APVTS caches must also agree with Y");
        expect(! processor.hasPendingStateNotificationsForTesting());
        logMessage("  held-listener restore returned; authoritative Y immediate; raw Y after replay; no pending notifications");
    }
};

static ReverseEngineTests reverseEngineTests;
static ProcessorTests processorTests;
static AcceptanceRegressionTests acceptanceRegressionTests;
static StateTransactionTests stateTransactionTests;
static ListenerLockRegressionTests listenerLockRegressionTests;

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    juce::UnitTestRunner runner;
    if (argc == 2 && juce::String(argv[1]) == "--state-transactions-only")
        runner.runTestsInCategory("StateTransactions");
    else if (argc == 2 && juce::String(argv[1]) == "--listener-lock-only")
        runner.runTestsInCategory("ListenerLock");
    else
        runner.runAllTests();
    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        if (auto* result = runner.getResult(i)) failures += result->failures;
    return failures == 0 ? 0 : 1;
}
