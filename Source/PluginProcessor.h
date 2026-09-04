#pragma once
#include "PresetLibrary.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdint>
#include <mutex>
#include "Parameters.h"
#include "ReverseEngine.h"

class ReverseLabAudioProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    explicit ReverseLabAudioProcessor(juce::File presetStorage = {});
    ~ReverseLabAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
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

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram.load(std::memory_order_acquire); }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    wk::PresetLibrary presets;
    [[nodiscard]] int getCurrentLatencySamples() const noexcept { return displayedLatency.load(); }
    [[nodiscard]] float getScopeSample(int channel, int index) const noexcept;
    [[nodiscard]] int getScopeWriteIndex() const noexcept { return scopeWrite.load(); }
    [[nodiscard]] float getEnginePhase(int channel) const noexcept { return engine.getNormalizedPhase(channel); }
    [[nodiscard]] juce::Point<int> getLastEditorSize() const noexcept
    {
        const auto packed = packedEditorSize.load(std::memory_order_acquire);
        return { static_cast<int>(static_cast<std::uint32_t>((packed & ~editorSizeRestorePendingMask) >> 32)),
                 static_cast<int>(static_cast<std::uint32_t>(packed)) };
    }
    void setLastEditorSize(int width, int height) noexcept
    {
        const auto desired = packEditorSize(width, height);
        auto current = packedEditorSize.load(std::memory_order_acquire);
        while ((current & editorSizeRestorePendingMask) == 0
               && ! packedEditorSize.compare_exchange_weak(current, desired,
                                                            std::memory_order_acq_rel,
                                                            std::memory_order_acquire))
        {
        }
    }
    void setRestoredEditorSize(int width, int height) noexcept
    {
        packedEditorSize.store(packEditorSize(width, height) | editorSizeRestorePendingMask,
                               std::memory_order_release);
    }
    void acknowledgeRestoredEditorSize(int width, int height) noexcept
    {
        auto expected = packEditorSize(width, height) | editorSizeRestorePendingMask;
        const auto acknowledged = packEditorSize(width, height);
        static_cast<void>(packedEditorSize.compare_exchange_strong(expected, acknowledged,
                                                                   std::memory_order_acq_rel,
                                                                   std::memory_order_acquire));
    }
#if REVERSELAB_UNIT_TESTS
    void servicePendingHostUpdatesForTesting() { timerCallback(); }
    [[nodiscard]] bool hasPendingStateNotificationsForTesting() const noexcept
    {
        return controlGeneration.load(std::memory_order_acquire)
               != notifiedGeneration.load(std::memory_order_acquire);
    }
    [[nodiscard]] size_t getAllocatedHistoryBytesForTesting() const noexcept
    {
        return engine.getAllocatedStorageBytes()
               + static_cast<size_t>(dryDelay.getNumChannels()) * static_cast<size_t>(dryDelay.getNumSamples())
                     * sizeof(float)
               + static_cast<size_t>(wetAlignmentDelay.getNumChannels())
                     * static_cast<size_t>(wetAlignmentDelay.getNumSamples()) * sizeof(float);
    }
#endif

private:
    struct ControlOperation
    {
        enum class Kind { program, state };
        Kind kind = Kind::program;
        int program = 0;
        juce::ValueTree state;
    };

    struct OnePoleState { float low = 0.0f; float highLow = 0.0f; };
    // Per-channel wet alignment tap. The offset depends on the host-acknowledged latency *and* the
    // engine's active segment length, so it can change at a segment boundary before the host has
    // acknowledged a new latency. Every offset change is crossfaded instead of switched hard.
    struct WetTap { int current = 0; int previous = 0; int transitionRemaining = 0; };
    int calculateLengthSamples(int choice, float freeMs, double bpm, double beatsPerBar, bool sync) const noexcept;
    float processFilters(int channel, float input, float hpHz, float lpHz,
                         float hpAmount, float lpAmount) noexcept;
    void queueLatencyUpdate(int samples) noexcept;
    void timerCallback() override;
    void invalidateDelayLines() noexcept;
    void submitControlOperation(ControlOperation);
    void drainStateNotifications();
    void applyRestoredEditorSize();
    float readParameter(rl::params::Index) const noexcept;
    float runtimeParameter(rl::params::Index index) const noexcept
    {
        return runtimeState.values[static_cast<size_t>(index)];
    }
    void refreshRuntimeState() noexcept;

    static constexpr std::uint64_t editorSizeRestorePendingMask = std::uint64_t { 1 } << 63;
    static constexpr std::uint64_t packEditorSize(int width, int height) noexcept
    {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(width)) << 32)
               | static_cast<std::uint64_t>(static_cast<std::uint32_t>(height));
    }

    rl::ReverseEngine engine;
    juce::AudioBuffer<float> dryDelay;
    juce::AudioBuffer<float> wetAlignmentDelay;
    std::array<OnePoleState, 2> filterState;
    std::array<WetTap, 2> wetTaps;
    std::array<std::array<std::atomic<float>, 256>, 2> scope {};
    std::atomic<int> scopeWrite { 0 };
    std::atomic<int> displayedLatency { 0 };
    std::atomic<int> pendingLatency { 0 };
    std::atomic<int> acknowledgedLatency { 0 };
    std::atomic<double> publishedBpm { 120.0 };
    std::atomic<double> publishedBeatsPerBar { 4.0 };
    std::atomic<bool> retriggerResetRequested { false };
    std::atomic<int> currentProgram { 0 };
    std::atomic<int> pendingProgramRequest { -1 };
    static constexpr size_t parameterCount = rl::params::ids.size();
    std::array<juce::RangedAudioParameter*, parameterCount> rangedParameters {};
    // The control gate never calls APVTS, listeners, the host, or an editor. A
    // caller holding a JUCE parameter-listener lock can therefore commit without
    // waiting for the notification dispatcher which may need that same lock.
    std::mutex controlMutex;
    juce::ValueTree stateExtensions { "ReverseLabState" }; // immutable between commits
    bool committedProgramNotification = false; // protected by controlMutex
    std::atomic<std::uint64_t> controlGeneration { 0 }, notifiedGeneration { 0 };
    std::atomic<juce::Thread::ThreadID> notificationOwner { nullptr };
    static constexpr unsigned maxNotificationsPerDrain = 64;
    // Single-attempt seqlock read: processBlock never waits or takes a mutex.
    // Actual ranged parameters are authoritative; APVTS raw values are UI caches.
    static_assert(std::atomic<float>::is_always_lock_free
                  && std::atomic<std::uint64_t>::is_always_lock_free,
                  "The DSP control packet requires lock-free parameter and sequence atomics");
    std::atomic<std::uint64_t> parameterSequence { 0 }, resetGeneration { 0 };
    struct RuntimeState
    {
        std::array<float, parameterCount> values {};
        std::uint64_t reset = 0;
    };
    RuntimeState runtimeState; // audio/lifecycle thread only
    std::uint64_t appliedResetGeneration = 0;
    juce::SmoothedValue<float> smoothedMix, smoothedOutput, smoothedBypass, smoothedSpeed,
                               smoothedCrossfade, smoothedFeedback, smoothedHighpass,
                               smoothedLowpass, smoothedHighpassEnabled, smoothedLowpassEnabled,
                               smoothedOffset, smoothedRandom;
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
    int previousBlockSamples = 0;
    bool lastRetriggerParameter = false;
    uint32_t appliedSeed = 0;
    std::atomic<std::uint64_t> packedEditorSize { (std::uint64_t { 900 } << 32) | std::uint64_t { 610 } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverseLabAudioProcessor)
};
