#include "PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
#include <iomanip>

namespace
{
void setParameter(ReverseLabAudioProcessor& processor, const char* id, float plainValue)
{
    auto* parameter = processor.parameters.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
}

bool writeWave(const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr) return false;
    juce::WavAudioFormat format;
    const auto options = juce::AudioFormatWriterOptions {}
                             .withSampleRate(sampleRate)
                             .withChannelLayout(juce::AudioChannelSet::stereo())
                             .withBitsPerSample(24);
    auto writer = format.createWriterFor(stream, options);
    return writer != nullptr && writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
}
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    if (argc != 4)
    {
        std::cerr << "usage: ReverseLabRenderAB source.aiff dry.wav wet.wav\n";
        return 2;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const auto sourceFile = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(sourceFile));
    if (reader == nullptr) return 3;

    const auto sampleRate = reader->sampleRate;
    const auto latency = static_cast<int>(std::round(sampleRate * 2.0)); // 1 bar at 120 BPM
    const auto sourceSamples = static_cast<int>(reader->lengthInSamples);
    const auto totalSamples = sourceSamples + latency;
    juce::AudioBuffer<float> source(2, sourceSamples), dry(2, totalSamples), wet(2, totalSamples);
    source.clear(); dry.clear(); wet.clear();
    reader->read(&source, 0, sourceSamples, 0, true, reader->numChannels > 1);
    if (reader->numChannels == 1) source.copyFrom(1, 0, source, 0, 0, sourceSamples);
    for (int channel = 0; channel < 2; ++channel)
    {
        dry.copyFrom(channel, latency, source, channel, 0, sourceSamples);
        dry.applyGain(channel, latency, sourceSamples, juce::Decibels::decibelsToGain(-1.0f));
    }

    ReverseLabAudioProcessor processor;
    setParameter(processor, rl::params::sync, 1.0f);
    setParameter(processor, rl::params::link, 0.0f);
    setParameter(processor, rl::params::leftSize, 0.0f);   // 1/32
    setParameter(processor, rl::params::rightSize, 13.0f); // 1 Bar
    setParameter(processor, rl::params::speed, 1.0f);
    setParameter(processor, rl::params::crossfade, 2.0f);
    setParameter(processor, rl::params::mix, 100.0f);
    setParameter(processor, rl::params::output, -1.0f);
    setParameter(processor, rl::params::feedback, 0.0f);
    setParameter(processor, rl::params::stereoOffset, 0.0f);
    setParameter(processor, rl::params::random, 0.0f);
    processor.prepareToPlay(sampleRate, 256);

    juce::MidiBuffer midi;
    for (int position = 0; position < totalSamples; position += 256)
    {
        const auto count = juce::jmin(256, totalSamples - position);
        juce::AudioBuffer<float> block(2, count);
        block.clear();
        if (position < sourceSamples)
        {
            const auto available = juce::jmin(count, sourceSamples - position);
            for (int channel = 0; channel < 2; ++channel)
                block.copyFrom(channel, 0, source, channel, position, available);
        }
        processor.processBlock(block, midi);
        for (int channel = 0; channel < 2; ++channel)
            wet.copyFrom(channel, position, block, channel, 0, count);
    }

    const auto dryFile = juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);
    const auto wetFile = juce::File::getCurrentWorkingDirectory().getChildFile(argv[3]);
    if (!writeWave(dryFile, dry, sampleRate) || !writeWave(wetFile, wet, sampleRate)) return 4;
    auto rms = [latency, totalSamples](const juce::AudioBuffer<float>& audio, int channel)
    {
        double sum = 0.0;
        for (int i = latency; i < totalSamples; ++i) { const auto x = audio.getSample(channel, i); sum += x * x; }
        return std::sqrt(sum / static_cast<double>(totalSamples - latency));
    };
    auto differenceRms = [latency, totalSamples](const juce::AudioBuffer<float>& a, int ca,
                                                  const juce::AudioBuffer<float>& b, int cb)
    {
        double sum = 0.0;
        for (int i = latency; i < totalSamples; ++i)
        {
            const auto d = a.getSample(ca, i) - b.getSample(cb, i); sum += d * d;
        }
        return std::sqrt(sum / static_cast<double>(totalSamples - latency));
    };
    auto correlation = [latency, totalSamples](const juce::AudioBuffer<float>& audio)
    {
        double dot = 0.0, ll = 0.0, rr = 0.0;
        for (int i = latency; i < totalSamples; ++i)
        {
            const auto l = audio.getSample(0, i), r = audio.getSample(1, i);
            dot += l * r; ll += l * l; rr += r * r;
        }
        return dot / std::sqrt(ll * rr);
    };
    std::cout << std::fixed << std::setprecision(6)
              << "dry_file=" << dryFile.getFullPathName() << "\n"
              << "wet_file=" << wetFile.getFullPathName() << "\n"
              << "sample_rate=" << sampleRate << "\n"
              << "duration_seconds=" << static_cast<double>(totalSamples) / sampleRate << "\n"
              << "dry_rms=" << rms(dry, 0) << "\n"
              << "wet_left_rms=" << rms(wet, 0) << "\n"
              << "wet_right_rms=" << rms(wet, 1) << "\n"
              << "dry_lr_correlation=" << correlation(dry) << "\n"
              << "wet_lr_correlation=" << correlation(wet) << "\n"
              << "wet_lr_difference_rms=" << differenceRms(wet, 0, wet, 1) << "\n"
              << "dry_vs_wet_left_difference_rms=" << differenceRms(dry, 0, wet, 0) << "\n"
              << "dry_vs_wet_right_difference_rms=" << differenceRms(dry, 1, wet, 1) << "\n";
    return 0;
}
