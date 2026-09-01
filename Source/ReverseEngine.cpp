#include "ReverseEngine.h"
#include <algorithm>
#include <cmath>

namespace rl
{
void ReverseEngine::prepare(double newSampleRate, int maximumLengthSamples)
{
    sampleRate = newSampleRate;
    capacity = juce::jmax(4096, maximumLengthSamples + 8);
    ring.setSize(2, capacity, false, true, false);
    for (auto& tags : generations)
        tags.assign(static_cast<size_t>(capacity), 0u);
    generation = 1;
    reset();
}

void ReverseEngine::reset() noexcept
{
    if (++generation == 0)
    {
        for (auto& tags : generations)
            std::fill(tags.begin(), tags.end(), 0u);
        generation = 1;
    }
    writePosition = 0;
    for (size_t channel = 0; channel < heads.size(); ++channel)
    {
        heads[channel] = {};
        publishedPhase[channel].store(0.0f, std::memory_order_relaxed);
    }
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

float ReverseEngine::readInterpolated(int channel, float position) const noexcept
{
    while (position < 0.0f) position += static_cast<float>(capacity);
    while (position >= static_cast<float>(capacity)) position -= static_cast<float>(capacity);
    const auto p0 = static_cast<int>(position);
    const auto frac = position - static_cast<float>(p0);
    channel = juce::jlimit(0, 1, channel);
    const auto* data = ring.getReadPointer(channel);
    const auto sampleAt = [this, channel, data](int index) noexcept
    {
        index = wrap(index);
        return generations[(size_t) channel][(size_t) index] == generation ? data[index] : 0.0f;
    };
    const auto y0 = sampleAt(p0 - 1);
    const auto y1 = sampleAt(p0);
    const auto y2 = sampleAt(p0 + 1);
    const auto y3 = sampleAt(p0 + 2);
    const auto c0 = y1;
    const auto c1 = 0.5f * (y2 - y0);
    const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

float ReverseEngine::distanceFromWriter(float readPosition) const noexcept
{
    auto distance = std::fmod(readPosition - static_cast<float>(writePosition),
                              static_cast<float>(capacity));
    if (distance < 0.0f) distance += static_cast<float>(capacity);
    return distance;
}

void ReverseEngine::beginSegment(int channel, int requestedLength, const EngineSettings& settings) noexcept
{
    auto& head = heads[(size_t) channel];
    head.activeLength = juce::jlimit(16, capacity - 8,
                                     requestedLength);
    const auto stereo = channel == 0 ? -settings.stereoOffset : settings.stereoOffset;
    const auto randomOffset = static_cast<int>((nextRandom() * 2.0f - 1.0f)
                                                * settings.randomAmount * 0.5f * head.activeLength);
    const auto channelOffset = static_cast<int>(stereo * 0.5f * head.activeLength);
    head.segmentEnd = wrap(writePosition - 1 - randomOffset - channelOffset);
    head.phase = 0.0f;
    head.readOffset = 0.0f;
    head.transitionPhase = 0.0f;
    head.lastRead = 0.0f;
    head.historyRemaining = distanceFromWriter(static_cast<float>(head.segmentEnd));
    head.readExhausted = false;
    head.nextPrepared = false;
}

void ReverseEngine::prepareNextSegment(int channel, int requestedLength, const EngineSettings& settings) noexcept
{
    auto& head = heads[(size_t) channel];
    head.nextLength = juce::jlimit(16, capacity - 8,
                                  requestedLength);
    const auto stereo = channel == 0 ? -settings.stereoOffset : settings.stereoOffset;
    const auto randomOffset = static_cast<int>((nextRandom() * 2.0f - 1.0f)
                                                * settings.randomAmount * 0.5f * head.nextLength);
    const auto channelOffset = static_cast<int>(stereo * 0.5f * head.nextLength);
    head.nextEnd = wrap(writePosition - 1 - randomOffset - channelOffset);
    head.nextLastRead = 0.0f;
    head.nextHistoryRemaining = distanceFromWriter(static_cast<float>(head.nextEnd));
    head.nextReadExhausted = false;
    head.nextPrepared = true;
}

float ReverseEngine::readCaptured(int channel, int end, float offset, float speed, bool mayOverwrite,
                                  float& remaining, float& lastRead, bool& exhausted) const noexcept
{
    if (mayOverwrite)
    {
        // Reader and writer approach at (speed + 1) samples per sample. The remaining distance
        // is paused during Freeze and rebased when writing resumes, because Freeze advances the
        // logical write position without replacing any ring samples.
        if (remaining <= 8.0f) exhausted = true;
        remaining -= speed + 1.0f;
    }

    if (!exhausted)
        lastRead = readInterpolated(channel, static_cast<float>(end) - offset);
    return lastRead;
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
            // Continue exactly where the incoming segment was being read during the crossfade.
            head.segmentEnd = head.nextEnd;
            head.activeLength = head.nextLength;
            head.phase = head.transitionPhase;
            head.readOffset = head.nextOffset;
            head.lastRead = head.nextLastRead;
            head.historyRemaining = head.nextHistoryRemaining;
            head.readExhausted = head.nextReadExhausted;
            head.nextPrepared = false;
        }
        else
            beginSegment(channel, requested, settings);
    }

    // The read offset is integrated per sample (readOffset += speed) instead of being derived
    // from phase * speed, so speed automation changes the read velocity, not the read position.
    // Rebase the overwrite distance when leaving Freeze. No samples were replaced while frozen,
    // even though both the logical writer and the reverse reader continued moving.
    if (head.wasFrozen && !settings.freeze)
    {
        if (!head.readExhausted)
            head.historyRemaining = distanceFromWriter(static_cast<float>(head.segmentEnd)
                                                        - head.readOffset);
        if (head.nextPrepared && !head.nextReadExhausted)
            head.nextHistoryRemaining = distanceFromWriter(static_cast<float>(head.nextEnd)
                                                            - head.nextOffset);
    }
    head.wasFrozen = settings.freeze;

    // At high speeds, hold the final valid sample once writer and reader meet instead of wrapping
    // around into material written during the current segment.
    auto wet = readCaptured(channel, head.segmentEnd, head.readOffset, settings.speed,
                            !settings.freeze, head.historyRemaining, head.lastRead,
                            head.readExhausted);

    const auto fadeSamples = juce::jlimit(1.0f, head.activeLength * 0.25f,
                                         head.activeLength * settings.crossfade);
    const auto transitionStart = static_cast<float>(head.activeLength) - fadeSamples;
    if (head.phase >= transitionStart)
    {
        if (!head.nextPrepared)
        {
            prepareNextSegment(channel, requested, settings);
            head.nextOffset = 0.0f;
            head.transitionPhase = 0.0f;
        }
        const auto transition = juce::jlimit(0.0f, 1.0f, head.transitionPhase / fadeSamples);
        const auto nextWet = readCaptured(channel, head.nextEnd, head.nextOffset, settings.speed,
                                          !settings.freeze, head.nextHistoryRemaining,
                                          head.nextLastRead, head.nextReadExhausted);
        wet = wet * std::cos(transition * juce::MathConstants<float>::halfPi)
              + nextWet * std::sin(transition * juce::MathConstants<float>::halfPi);
        head.nextOffset += settings.speed;
        head.transitionPhase += 1.0f;
    }

    if (settings.speed > 1.0f)
    {
        const auto cutoff = static_cast<float>(sampleRate) * 0.45f / settings.speed;
        const auto coefficient = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * cutoff
                                                  / static_cast<float>(sampleRate));
        head.antiAliasState += coefficient * (wet - head.antiAliasState);
        wet = head.antiAliasState;
    }
    else
        head.antiAliasState = wet;

    if (!settings.freeze)
    {
        const auto write = juce::jlimit(-4.0f, 4.0f, input + wet * settings.feedback);
        ring.setSample(channel, writePosition, std::isfinite(write) ? write : 0.0f);
        generations[(size_t) channel][(size_t) writePosition] = generation;
    }

    head.phase += 1.0f;
    head.readOffset += settings.speed;
    publishedPhase[(size_t) channel].store(
        juce::jlimit(0.0f, 1.0f, head.phase / static_cast<float>(juce::jmax(1, head.activeLength))),
        std::memory_order_relaxed);
    return std::isfinite(wet) ? wet : 0.0f;
}

void ReverseEngine::advance() noexcept
{
    writePosition = (writePosition + 1) % capacity;
}
} // namespace rl
