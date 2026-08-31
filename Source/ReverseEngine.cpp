#include "ReverseEngine.h"
#include <cmath>

namespace rl
{
void ReverseEngine::prepare(double newSampleRate, int maximumLengthSamples)
{
    sampleRate = newSampleRate;
    capacity = juce::jmax(4096, maximumLengthSamples + 8);
    ring.setSize(2, capacity, false, true, false);
    reset();
}

void ReverseEngine::reset() noexcept
{
    ring.clear();
    writePosition = 0;
    for (auto& head : heads)
        head = {};
}

void ReverseEngine::setSeed(uint32_t seed) noexcept
{
    randomState = seed == 0 ? 1u : seed;
}

float ReverseEngine::nextRandom() noexcept
{
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return static_cast<float>(randomState & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

int ReverseEngine::wrap(int position) const noexcept
{
    position %= capacity;
    return position < 0 ? position + capacity : position;
}

float ReverseEngine::readLinear(int channel, float position) const noexcept
{
    while (position < 0.0f) position += static_cast<float>(capacity);
    while (position >= static_cast<float>(capacity)) position -= static_cast<float>(capacity);
    const auto p0 = static_cast<int>(position);
    const auto p1 = (p0 + 1) % capacity;
    const auto frac = position - static_cast<float>(p0);
    const auto* data = ring.getReadPointer(juce::jlimit(0, 1, channel));
    return data[p0] + frac * (data[p1] - data[p0]);
}

void ReverseEngine::beginSegment(int channel, int requestedLength, const EngineSettings& settings) noexcept
{
    auto& head = heads[(size_t) channel];
    const auto variation = (nextRandom() * 2.0f - 1.0f) * settings.randomAmount * 0.25f;
    head.activeLength = juce::jlimit(16, capacity - 8,
                                     static_cast<int>(static_cast<float>(requestedLength) * (1.0f + variation)));
    const auto stereo = channel == 0 ? -settings.stereoOffset : settings.stereoOffset;
    const auto randomOffset = static_cast<int>((nextRandom() * 2.0f - 1.0f)
                                                * settings.randomAmount * 0.5f * head.activeLength);
    const auto channelOffset = static_cast<int>(stereo * 0.5f * head.activeLength);
    head.segmentEnd = wrap(writePosition - 1 - randomOffset - channelOffset);
    head.phase = 0.0f;
    head.nextPrepared = false;
}

void ReverseEngine::prepareNextSegment(int channel, int requestedLength, const EngineSettings& settings) noexcept
{
    auto& head = heads[(size_t) channel];
    const auto variation = (nextRandom() * 2.0f - 1.0f) * settings.randomAmount * 0.25f;
    head.nextLength = juce::jlimit(16, capacity - 8,
                                  static_cast<int>(static_cast<float>(requestedLength) * (1.0f + variation)));
    const auto stereo = channel == 0 ? -settings.stereoOffset : settings.stereoOffset;
    const auto randomOffset = static_cast<int>((nextRandom() * 2.0f - 1.0f)
                                                * settings.randomAmount * 0.5f * head.nextLength);
    const auto channelOffset = static_cast<int>(stereo * 0.5f * head.nextLength);
    head.nextEnd = wrap(writePosition - 1 - randomOffset - channelOffset);
    head.nextPrepared = true;
}

float ReverseEngine::processSample(int channel, float input, const EngineSettings& settings) noexcept
{
    channel = juce::jlimit(0, 1, channel);
    auto& head = heads[(size_t) channel];
    const int requested = channel == 0 ? settings.leftLength : settings.rightLength;
    const bool triggerEdge = settings.retrigger && !head.lastRetrigger;
    head.lastRetrigger = settings.retrigger;
    if (head.activeLength <= 1 || triggerEdge)
        beginSegment(channel, requested, settings);

    if (head.phase >= static_cast<float>(head.activeLength))
    {
        if (head.nextPrepared)
        {
            const auto fadeAdvance = juce::jlimit(1.0f, head.activeLength * 0.25f,
                                                  head.activeLength * settings.crossfade);
            head.segmentEnd = head.nextEnd;
            head.activeLength = head.nextLength;
            head.phase = fadeAdvance;
            head.nextPrepared = false;
        }
        else
            beginSegment(channel, requested, settings);
    }

    const auto readPosition = static_cast<float>(head.segmentEnd) - head.phase * settings.speed;
    auto wet = readLinear(channel, readPosition);

    const auto fadeSamples = juce::jlimit(1.0f, head.activeLength * 0.25f,
                                         head.activeLength * settings.crossfade);
    const auto transitionStart = static_cast<float>(head.activeLength) - fadeSamples;
    if (head.phase >= transitionStart)
    {
        if (!head.nextPrepared) prepareNextSegment(channel, requested, settings);
        const auto transition = juce::jlimit(0.0f, 1.0f, (head.phase - transitionStart) / fadeSamples);
        const auto nextPosition = static_cast<float>(head.nextEnd) - (head.phase - transitionStart) * settings.speed;
        const auto nextWet = readLinear(channel, nextPosition);
        wet = wet * std::cos(transition * juce::MathConstants<float>::halfPi)
              + nextWet * std::sin(transition * juce::MathConstants<float>::halfPi);
    }

    if (!settings.freeze)
    {
        const auto write = juce::jlimit(-4.0f, 4.0f, input + wet * settings.feedback);
        ring.setSample(channel, writePosition, std::isfinite(write) ? write : 0.0f);
    }

    head.phase += 1.0f;
    return std::isfinite(wet) ? wet : 0.0f;
}

void ReverseEngine::advance() noexcept
{
    writePosition = (writePosition + 1) % capacity;
}
} // namespace rl
