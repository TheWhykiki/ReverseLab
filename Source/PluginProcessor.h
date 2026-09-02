#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "ReverseEngine.h"

class ReverseLabAudioProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    ReverseLabAudioProcessor();
    ~ReverseLabAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    juce::AudioProcessorParameter* getBypassParameter() const override
    {
        return parameters.getParameter(rl::params::bypass);
    }

    int getNumPrograms() override { return 6; }
    int getCurrentProgram() override { return currentProgram.load(std::memory_order_acquire); }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    [[nodiscard]] int getCurrentLatencySamples() const noexcept { return displayedLatency.load(); }
    [[nodiscard]] float getScopeSample(int index) const noexcept;
    [[nodiscard]] int getScopeWriteIndex() const noexcept { return scopeWrite.load(); }
    [[nodiscard]] float getEnginePhase(int channel) const noexcept { return engine.getNormalizedPhase(channel); }
    [[nodiscard]] juce::Point<int> getLastEditorSize() const noexcept
    {
        return { editorWidth.load(std::memory_order_relaxed),
                 editorHeight.load(std::memory_order_relaxed) };
    }
    void setLastEditorSize(int width, int height) noexcept
    {
        editorWidth.store(width, std::memory_order_relaxed);
        editorHeight.store(height, std::memory_order_relaxed);
    }
#if REVERSELAB_UNIT_TESTS
    void servicePendingHostUpdatesForTesting() { timerCallback(); }
#endif

private:
    struct OnePoleState { float low = 0.0f; float highLow = 0.0f; };
    // Per-channel wet alignment tap. The offset depends on the host-acknowledged latency *and* the
    // engine's active segment length, so it can change at a segment boundary before the host has
    // acknowledged a new latency. Every offset change is crossfaded instead of switched hard.
    struct WetTap { int current = 0; int previous = 0; int transitionRemaining = 0; };
    int calculateLengthSamples(int choice, float freeMs, double bpm, double beatsPerBar, bool sync) const noexcept;
    float processFilters(int channel, float input, float hpHz, float lpHz) noexcept;
    void queueLatencyUpdate(int samples) noexcept;
    void timerCallback() override;
    void invalidateDelayLines() noexcept;
    void setPlainParameter(const char* id, float plainValue);
    void applyPendingProgramChange();

    rl::ReverseEngine engine;
    juce::AudioBuffer<float> dryDelay;
    juce::AudioBuffer<float> wetAlignmentDelay;
    std::array<OnePoleState, 2> filterState;
    std::array<WetTap, 2> wetTaps;
    std::array<std::atomic<float>, 256> scope {};
    std::atomic<int> scopeWrite { 0 };
    std::atomic<int> displayedLatency { 0 };
    std::atomic<int> pendingLatency { 0 };
    std::atomic<int> acknowledgedLatency { 0 };
    std::atomic<double> publishedBpm { 120.0 };
    std::atomic<double> publishedBeatsPerBar { 4.0 };
    std::atomic<bool> retriggerResetRequested { false };
    std::atomic<bool> processingResetRequested { false };
    std::atomic<bool> programChangeRequested { false };
    std::atomic<int> currentProgram { 0 };
    std::atomic<int> pendingProgram { 0 };
    juce::SmoothedValue<float> smoothedMix, smoothedOutput, smoothedBypass, smoothedSpeed,
                               smoothedCrossfade, smoothedFeedback, smoothedHighpass,
                               smoothedLowpass, smoothedOffset, smoothedRandom;
    int dryWrite = 0, wetWrite = 0;
    int validDelaySamples = 0;
    int maximumDelay = 1;
    int activeProcessingLatency = 0;
    int previousProcessingLatency = 0;
    int latencyTransitionRemaining = 0;
    int latencyTransitionLength = 1;
    int retriggerCountdown = -1;
    double currentSampleRate = 44100.0;
    double smoothedBpm = 120.0;
    bool wasPlaying = false;
    std::optional<int64_t> previousBlockPosition;
    bool lastRetriggerParameter = false;
    uint32_t appliedSeed = 0;
    std::atomic<int> editorWidth { 900 };
    std::atomic<int> editorHeight { 610 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverseLabAudioProcessor)
};
