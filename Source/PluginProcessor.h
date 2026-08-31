#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "ReverseEngine.h"

class ReverseLabAudioProcessor final : public juce::AudioProcessor
{
public:
    ReverseLabAudioProcessor();
    ~ReverseLabAudioProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 32.0; }

    int getNumPrograms() override { return 6; }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    [[nodiscard]] int getCurrentLatencySamples() const noexcept { return displayedLatency.load(); }
    [[nodiscard]] float getScopeSample(int index) const noexcept;
    [[nodiscard]] int getScopeWriteIndex() const noexcept { return scopeWrite.load(); }
    [[nodiscard]] float getEnginePhase(int channel) const noexcept { return engine.getPhase(channel); }
    [[nodiscard]] juce::Point<int> getLastEditorSize() const noexcept { return { editorWidth, editorHeight }; }
    void setLastEditorSize(int width, int height) noexcept { editorWidth = width; editorHeight = height; }

private:
    struct OnePoleState { float low = 0.0f; float highLow = 0.0f; };
    int calculateLengthSamples(int choice, double bpm, bool sync) const noexcept;
    float processFilters(int channel, float input, float hpHz, float lpHz) noexcept;
    void updateLatency(int samples);
    void setPlainParameter(const char* id, float plainValue);

    rl::ReverseEngine engine;
    juce::AudioBuffer<float> dryDelay;
    std::array<OnePoleState, 2> filterState;
    std::array<std::atomic<float>, 256> scope {};
    std::atomic<int> scopeWrite { 0 };
    std::atomic<int> displayedLatency { 0 };
    juce::SmoothedValue<float> smoothedMix, smoothedOutput, smoothedBypass;
    int dryWrite = 0;
    int maximumDelay = 1;
    int currentProgram = 0;
    double currentSampleRate = 44100.0;
    double smoothedBpm = 120.0;
    bool wasPlaying = false;
    uint32_t appliedSeed = 0;
    int editorWidth = 900;
    int editorHeight = 610;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverseLabAudioProcessor)
};
