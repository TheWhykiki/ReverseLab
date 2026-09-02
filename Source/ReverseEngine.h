#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <vector>

namespace rl
{
struct EngineSettings
{
    int leftLength = 24000;
    int rightLength = 24000;
    float speed = 1.0f;
    float crossfade = 0.04f;
    float feedback = 0.0f;
    float stereoOffset = 0.0f;
    float randomAmount = 0.0f;
    bool freeze = false;
    bool retrigger = false;
};

class ReverseEngine
{
public:
    void prepare(double newSampleRate, int maximumLengthSamples);
    void reset() noexcept;
    float processSample(int channel, float input, const EngineSettings& settings) noexcept;
    [[nodiscard]] int getWritePosition() const noexcept { return writePosition; }
    [[nodiscard]] float getNormalizedPhase(int channel) const noexcept
    {
        return publishedPhase[(size_t) juce::jlimit(0, 1, channel)].load(std::memory_order_relaxed);
    }
    [[nodiscard]] int getActiveLength(int channel) const noexcept
    {
        return heads[(size_t) juce::jlimit(0, 1, channel)].activeLength;
    }
    void advance() noexcept;
    void setSeed(uint32_t seed) noexcept;

private:
    struct Head
    {
        float phase = 0.0f;
        float readOffset = 0.0f;       // samples read back from segmentEnd (accumulated, speed-integrated)
        float nextOffset = 0.0f;       // same for the incoming segment during the crossfade
        float transitionPhase = 0.0f;  // samples elapsed since the crossfade started
        float lastRead = 0.0f;
        float nextLastRead = 0.0f;
        float historyRemaining = 0.0f;
        float nextHistoryRemaining = 0.0f;
        int segmentEnd = 0;
        int activeLength = 1;
        int nextEnd = 0;
        int nextLength = 1;
        bool nextPrepared = false;
        bool readExhausted = false;
        bool nextReadExhausted = false;
        bool wasFrozen = false;
        bool lastRetrigger = false;
        float antiAliasState = 0.0f;
    };

    float readInterpolated(int channel, float position) const noexcept;
    float readCaptured(int channel, int end, float offset, float speed, bool mayOverwrite,
                       float& remaining, float& lastRead, bool& exhausted) const noexcept;
    float distanceFromWriter(float readPosition) const noexcept;
    void beginSegment(int channel, int requestedLength, const EngineSettings&) noexcept;
    void prepareNextSegment(int channel, int requestedLength, const EngineSettings&) noexcept;
    float nextRandom() noexcept;
    int wrap(int position) const noexcept;

    juce::AudioBuffer<float> ring;
    // Both channels are written at the same ring position before advance(), so a single
    // generation map is sufficient to invalidate the stereo frame after reset().
    std::vector<uint32_t> generations;
    std::array<Head, 2> heads;
    std::array<std::atomic<float>, 2> publishedPhase { 0.0f, 0.0f };
    int writePosition = 0;
    int capacity = 1;
    double sampleRate = 44100.0;
    uint32_t randomState = 4242;
    uint32_t generation = 1;
    bool wroteCurrentPosition = false;
};
} // namespace rl
